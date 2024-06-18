/*
 * Budget Fair Queueing (BFQ) disk scheduler.
 *
 * Based on ideas and code from CFQ:
 * Copyright (C) 2003 Jens Axboe <axboe@kernel.dk>
 *
 * Copyright (C) 2008 Fabio Checconi <fabio@gandalf.sssup.it>
 *		      Paolo Valente <paolo.valente@unimore.it>
 *
 * Copyright (C) 2010 Paolo Valente <paolo.valente@unimore.it>
 *
 * Licensed under the GPL-2 as detailed in the accompanying COPYING.BFQ
 * file.
 *
 * BFQ is a proportional-share storage-I/O scheduling algorithm based on
 * the slice-by-slice service scheme of CFQ. But BFQ assigns budgets,
 * measured in number of sectors, to processes instead of time slices. The
 * device is not granted to the in-service process for a given time slice,
 * but until it has exhausted its assigned budget. This change from the time
 * to the service domain allows BFQ to distribute the device throughput
 * among processes as desired, without any distortion due to ZBR, workload
 * fluctuations or other factors. BFQ uses an ad hoc internal scheduler,
 * called B-WF2Q+, to schedule processes according to their budgets. More
 * precisely, BFQ schedules queues associated to processes. Thanks to the
 * accurate policy of B-WF2Q+, BFQ can afford to assign high budgets to
 * I/O-bound processes issuing sequential requests (to boost the
 * throughput), and yet guarantee a low latency to interactive and soft
 * real-time applications.
 *
 * BFQ is described in [1], where also a reference to the initial, more
 * theoretical paper on BFQ can be found. The interested reader can find
 * in the latter paper full details on the main algorithm, as well as
 * formulas of the guarantees and formal proofs of all the properties.
 * With respect to the version of BFQ presented in these papers, this
 * implementation adds a few more heuristics, such as the one that
 * guarantees a low latency to soft real-time applications, and a
 * hierarchical extension based on H-WF2Q+.
 *
 * B-WF2Q+ is based on WF2Q+, that is described in [2], together with
 * H-WF2Q+, while the augmented tree used to implement B-WF2Q+ with O(log N)
 * complexity derives from the one introduced with EEVDF in [3].
 *
 * [1] P. Valente and M. Andreolini, ``Improving Application Responsiveness
 *     with the BFQ Disk I/O Scheduler'',
 *     Proceedings of the 5th Annual International Systems and Storage
 *     Conference (SYSTOR '12), June 2012.
 *
 * http://algogroup.unimo.it/people/paolo/disk_sched/bf1-v1-suite-results.pdf
 *
 * [2] Jon C.R. Bennett and H. Zhang, ``Hierarchical Packet Fair Queueing
 *     Algorithms,'' IEEE/ACM Transactions on Networking, 5(5):675-689,
 *     Oct 1997.
 *
 * http://www.cs.cmu.edu/~hzhang/papers/TON-97-Oct.ps.gz
 *
 * [3] I. Stoica and H. Abdel-Wahab, ``Earliest Eligible Virtual Deadline
 *     First: A Flexible and Accurate Mechanism for Proportional Share
 *     Resource Allocation,'' technical report.
 *
 * http://www.cs.berkeley.edu/~istoica/papers/eevdf-tr-95.pdf
 */
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/blkdev.h>
#include <linux/cgroup.h>
#include <linux/elevator.h>
#include <linux/jiffies.h>
#include <linux/rbtree.h>
#include <linux/ioprio.h>
#include "bfq.h"
#include "blk.h"

/* Expiration time of sync (0) and async (1) requests, in jiffies. */
static const int bfq_fifo_expire[2] = { HZ / 4, HZ / 8 };

/* Maximum backwards seek, in KiB. */
static const int bfq_back_max = 16 * 1024;

/* Penalty of a backwards seek, in number of sectors. */
static const int bfq_back_penalty = 2;

/* Idling period duration, in jiffies. */
static int bfq_slice_idle = HZ / 125;

/* Minimum number of assigned budgets for which stats are safe to compute. */
static const int bfq_stats_min_budgets = 194;

/* Default maximum budget values, in sectors and number of requests. */
static const int bfq_default_max_budget = 16 * 1024;
static const int bfq_max_budget_async_rq = 4;

/*
 * Async to sync throughput distribution is controlled as follows:
 * when an async request is served, the entity is charged the number
 * of sectors of the request, multiplied by the factor below
 */
static const int bfq_async_charge_factor = 10;

/* Default timeout values, in jiffies, approximating CFQ defaults. */
static const int bfq_timeout_sync = HZ / 8;
static int bfq_timeout_async = HZ / 25;

struct kmem_cache *bfq_pool;

/* Below this threshold (in ms), we consider thinktime immediate. */
#define BFQ_MIN_TT		2

/* hw_tag detection: parallel requests threshold and min samples needed. */
#define BFQ_HW_QUEUE_THRESHOLD	4
#define BFQ_HW_QUEUE_SAMPLES	32

#define BFQQ_SEEK_THR	 (sector_t)(8 * 1024)
#define BFQQ_SEEKY(bfqq) ((bfqq)->seek_mean > BFQQ_SEEK_THR)

/* Min samples used for peak rate estimation (for autotuning). */
#define BFQ_PEAK_RATE_SAMPLES	32

/* Shift used for peak rate fixed precision calculations. */
#define BFQ_RATE_SHIFT		16

/*
 * By default, BFQ computes the duration of the weight raising for
 * interactive applications automatically, using the following formula:
 * duration = (R / r) * T, where r is the peak rate of the device, and
 * R and T are two reference parameters.
 * In particular, R is the peak rate of the reference device (see below),
 * and T is a reference time: given the systems that are likely to be
 * installed on the reference device according to its speed class, T is
 * about the maximum time needed, under BFQ and while reading two files in
 * parallel, to load typical large applications on these systems.
 * In practice, the slower/faster the device at hand is, the more/less it
 * takes to load applications with respect to the reference device.
 * Accordingly, the longer/shorter BFQ grants weight raising to interactive
 * applications.
 *
 * BFQ uses four different reference pairs (R, T), depending on:
 * . whether the device is rotational or non-rotational;
 * . whether the device is slow, such as old or portable HDDs, as well as
 *   SD cards, or fast, such as newer HDDs and SSDs.
 *
 * The device's speed class is dynamically (re)detected in
 * bfq_update_peak_rate() every time the estimated peak rate is updated.
 *
 * In the following definitions, R_slow[0]/R_fast[0] and T_slow[0]/T_fast[0]
 * are the reference values for a slow/fast rotational device, whereas
 * R_slow[1]/R_fast[1] and T_slow[1]/T_fast[1] are the reference values for
 * a slow/fast non-rotational device. Finally, device_speed_thresh are the
 * thresholds used to switch between speed classes.
 * Both the reference peak rates and the thresholds are measured in
 * sectors/usec, left-shifted by BFQ_RATE_SHIFT.
 */
static int R_slow[2] = {1536, 10752};
static int R_fast[2] = {17415, 34791};
/*
 * To improve readability, a conversion function is used to initialize the
 * following arrays, which entails that they can be initialized only in a
 * function.
 */
static int T_slow[2];
static int T_fast[2];
static int device_speed_thresh[2];

#define BFQ_SERVICE_TREE_INIT	((struct bfq_service_tree)		\
				{ RB_ROOT, RB_ROOT, NULL, NULL, 0, 0 })

#define RQ_BIC(rq)		((struct bfq_io_cq *) (rq)->elv.priv[0])
#define RQ_BFQQ(rq)		((rq)->elv.priv[1])

static void bfq_schedule_dispatch(struct bfq_data *bfqd);

#include "bfq-ioc.c"
#include "bfq-sched.c"
#include "bfq-cgroup.c"

#define bfq_class_idle(bfqq)	((bfqq)->ioprio_class == IOPRIO_CLASS_IDLE)
#define bfq_class_rt(bfqq)	((bfqq)->ioprio_class == IOPRIO_CLASS_RT)

#define bfq_sample_valid(samples)	((samples) > 80)

/*
 * We regard a request as SYNC, if either it's a read or has the SYNC bit
 * set (in which case it could also be a direct WRITE).
 */
static int bfq_bio_sync(struct bio *bio)
{
	if (bio_data_dir(bio) == READ || (bio->bi_rw & REQ_SYNC))
		return 1;

	return 0;
}

/*
 * Scheduler run of queue, if there are requests pending and no one in the
 * driver that will restart queueing.
 */
static void bfq_schedule_dispatch(struct bfq_data *bfqd)
{
	if (bfqd->queued != 0) {
		bfq_log(bfqd, "schedule dispatch");
		kblockd_schedule_work(&bfqd->unplug_work);
	}
}

/*
 * Lifted from AS - choose which of rq1 and rq2 that is best served now.
 * We choose the request that is closesr to the head right now.  Distance
 * behind the head is penalized and only allowed to a certain extent.
 */
static struct request *bfq_choose_req(struct bfq_data *bfqd,
				      struct request *rq1,
				      struct request *rq2,
				      sector_t last)
{
	sector_t s1, s2, d1 = 0, d2 = 0;
	unsigned long back_max;
#define BFQ_RQ1_WRAP	0x01 /* request 1 wraps */
#define BFQ_RQ2_WRAP	0x02 /* request 2 wraps */
	unsigned wrap = 0; /* bit mask: requests behind the disk head? */

	if (!rq1 || rq1 == rq2)
		return rq2;
	if (!rq2)
		return rq1;

	if (rq_is_sync(rq1) && !rq_is_sync(rq2))
		return rq1;
	else if (rq_is_sync(rq2) && !rq_is_sync(rq1))
		return rq2;
	if ((rq1->cmd_flags & REQ_META) && !(rq2->cmd_flags & REQ_META))
		return rq1;
	else if ((rq2->cmd_flags & REQ_META) && !(rq1->cmd_flags & REQ_META))
		return rq2;

	s1 = blk_rq_pos(rq1);
	s2 = blk_rq_pos(rq2);

	/*
	 * By definition, 1KiB is 2 sectors.
	 */
	back_max = bfqd->bfq_back_max * 2;

	/*
	 * Strict one way elevator _except_ in the case where we allow
	 * short backward seeks which are biased as twice the cost of a
	 * similar forward seek.
	 */
	if (s1 >= last)
		d1 = s1 - last;
	else if (s1 + back_max >= last)
		d1 = (last - s1) * bfqd->bfq_back_penalty;
	else
		wrap |= BFQ_RQ1_WRAP;

	if (s2 >= last)
		d2 = s2 - last;
	else if (s2 + back_max >= last)
		d2 = (last - s2) * bfqd->bfq_back_penalty;
	else
		wrap |= BFQ_RQ2_WRAP;

	/* Found required data */

	/*
	 * By doing switch() on the bit mask "wrap" we avoid having to
	 * check two variables for all permutations: --> faster!
	 */
	switch (wrap) {
	case 0: /* common case for CFQ: rq1 and rq2 not wrapped */
		if (d1 < d2)
			return rq1;
		else if (d2 < d1)
			return rq2;
		else {
			if (s1 >= s2)
				return rq1;
			else
				return rq2;
		}

	case BFQ_RQ2_WRAP:
		return rq1;
	case BFQ_RQ1_WRAP:
		return rq2;
	case (BFQ_RQ1_WRAP|BFQ_RQ2_WRAP): /* both rqs wrapped */
	default:
		/*
		 * Since both rqs are wrapped,
		 * start with the one that's further behind head
		 * (--> only *one* back seek required),
		 * since back seek takes more time than forward.
		 */
		if (s1 <= s2)
			return rq1;
		else
			return rq2;
	}
}

/*
 * Tell whether there are active queues or groups with differentiated weights.
 */
static bool bfq_differentiated_weights(struct bfq_data *bfqd)
{
	/*
	 * For weights to differ, at least one of the trees must contain
	 * at least two nodes.
	 */
	return (!RB_EMPTY_ROOT(&bfqd->queue_weights_tree) &&
		(bfqd->queue_weights_tree.rb_node->rb_left ||
		 bfqd->queue_weights_tree.rb_node->rb_right)
#ifdef CONFIG_BFQ_GROUP_IOSCHED
	       ) ||
	       (!RB_EMPTY_ROOT(&bfqd->group_weights_tree) &&
		(bfqd->group_weights_tree.rb_node->rb_left ||
		 bfqd->group_weights_tree.rb_node->rb_right)
#endif
	       );
}

/*
 * The following function returns true if every queue must receive the
 * same share of the throughput (this condition is used when deciding
 * whether idling may be disabled, see the comments in the function
 * bfq_bfqq_may_idle()).
 *
 * Such a scenario occurs when:
 * 1) all active queues have the same weight,
 * 2) all active groups at the same level in the groups tree have the same
 *    weight,
 * 3) all active groups at the same level in the groups tree have the same
 *    number of children.
 *
 * Unfortunately, keeping the necessary state for evaluating exactly the
 * above symmetry conditions would be quite complex and time-consuming.
 * Therefore this function evaluates, instead, the following stronger
 * sub-conditions, for which it is much easier to maintain the needed
 * state:
 * 1) all active queues have the same weight,
 * 2) all active groups have the same weight,
 * 3) all active groups have at most one active child each.
 * In particular, the last two conditions are always true if hierarchical
 * support and the cgroups interface are not enabled, thus no state needs
 * to be maintained in this case.
 */
static bool bfq_symmetric_scenario(struct bfq_data *bfqd)
{
	return
#ifdef CONFIG_BFQ_GROUP_IOSCHED
		!bfqd->active_numerous_groups &&
#endif
		!bfq_differentiated_weights(bfqd);
}

/*
 * If the weight-counter tree passed as input contains no counter for
 * the weight of the input entity, then add that counter; otherwise just
 * increment the existing counter.
 *
 * Note that weight-counter trees contain few nodes in mostly symmetric
 * scenarios. For example, if all queues have the same weight, then the
 * weight-counter tree for the queues may contain at most one node.
 * This holds even if low_latency is on, because weight-raised queues
 * are not inserted in the tree.
 * In most scenarios, the rate at which nodes are created/destroyed
 * should be low too.
 */
static void bfq_weights_tree_add(struct bfq_data *bfqd,
				 struct bfq_entity *entity,
				 struct rb_root *root)
{
	struct rb_node **new = &(root->rb_node), *parent = NULL;

	/*
	 * Do not insert if the entity is already associated with a
	 * counter, which happens if:
	 *   1) the entity is associated with a queue,
	 *   2) a request arrival has caused the queue to become both
	 *      non-weight-raised, and hence change its weight, and
	 *      backlogged; in this respect, each of the two events
	 *      causes an invocation of this function,
	 *   3) this is the invocation of this function caused by the
	 *      second event. This second invocation is actually useless,
	 *      and we handle this fact by exiting immediately. More
	 *      efficient or clearer solutions might possibly be adopted.
	 */
	if (entity->weight_counter)
		return;

	while (*new) {
		struct bfq_weight_counter *__counter = container_of(*new,
						struct bfq_weight_counter,
						weights_node);
		parent = *new;

		if (entity->weight == __counter->weight) {
			entity->weight_counter = __counter;
			goto inc_counter;
		}
		if (entity->weight < __counter->weight)
			new = &((*new)->rb_left);
		else
			new = &((*new)->rb_right);
	}

	entity->weight_counter = kzalloc(sizeof(struct bfq_weight_counter),
					 GFP_ATOMIC);
	entity->weight_counter->weight = entity->weight;
	rb_link_node(&entity->weight_counter->weights_node, parent, new);
	rb_insert_color(&entity->weight_counter->weights_node, root);

inc_counter:
	entity->weight_counter->num_active++;
}

/*
 * Decrement the weight counter associated with the entity, and, if the
 * counter reaches 0, remove the counter from the tree.
 * See the comments to the function bfq_weights_tree_add() for considerations
 * about overhead.
 */
static void bfq_weights_tree_remove(struct bfq_data *bfqd,
				    struct bfq_entity *entity,
				    struct rb_root *root)
{
	if (!entity->weight_counter)
		return;

	BUG_ON(RB_EMPTY_ROOT(root));
	BUG_ON(entity->weight_counter->weight != entity->weight);

	BUG_ON(!entity->weight_counter->num_active);
	entity->weight_counter->num_active--;
	if (entity->weight_counter->num_active > 0)
		goto reset_entity_pointer;

	rb_erase(&entity->weight_counter->weights_node, root);
	kfree(entity->weight_counter);

reset_entity_pointer:
	entity->weight_counter = NULL;
}

static struct request *bfq_find_next_rq(struct bfq_data *bfqd,
					struct bfq_queue *bfqq,
					struct request *last)
{
	struct rb_node *rbnext = rb_next(&last->rb_node);
	struct rb_node *rbprev = rb_prev(&last->rb_node);
	struct request *next = NULL, *prev = NULL;

	BUG_ON(RB_EMPTY_NODE(&last->rb_node));

	if (rbprev)
		prev = rb_entry_rq(rbprev);

	if (rbnext)
		next = rb_entry_rq(rbnext);
	else {
		rbnext = rb_first(&bfqq->sort_list);
		if (rbnext && rbnext != &last->rb_node)
			next = rb_entry_rq(rbnext);
	}

	return bfq_choose_req(bfqd, next, prev, blk_rq_pos(last));
}

/* see the definition of bfq_async_charge_factor for details */
static unsigned long bfq_serv_to_charge(struct request *rq,
					struct bfq_queue *bfqq)
{
	return blk_rq_sectors(rq) *
		(1 + ((!bfq_bfqq_sync(bfqq)) * (bfqq->wr_coeff == 1) *
		bfq_async_charge_factor));
}

/**
 * bfq_updated_next_req - update the queue after a new next_rq selection.
 * @bfqd: the device data the queue belongs to.
 * @bfqq: the queue to update.
 *
 * If the first request of a queue changes we make sure that the queue
 * has enough budget to serve at least its first request (if the
 * request has grown).  We do this because if the queue has not enough
 * budget for its first request, it has to go through two dispatch
 * rounds to actually get it dispatched.
 */
static void bfq_updated_next_req(struct bfq_data *bfqd,
				 struct bfq_queue *bfqq)
{
	struct bfq_entity *entity = &bfqq->entity;
	struct bfq_service_tree *st = bfq_entity_service_tree(entity);
	struct request *next_rq = bfqq->next_rq;
	unsigned long new_budget;

	if (!next_rq)
		return;

	if (bfqq == bfqd->in_service_queue)
		/*
		 * In order not to break guarantees, budgets cannot be
		 * changed after an entity has been selected.
		 */
		return;

	BUG_ON(entity->tree != &st->active);
	BUG_ON(entity == entity->sched_data->in_service_entity);

	new_budget = max_t(unsigned long, bfqq->max_budget,
			   bfq_serv_to_charge(next_rq, bfqq));
	if (entity->budget != new_budget) {
		entity->budget = new_budget;
		bfq_log_bfqq(bfqd, bfqq, "updated next rq: new budget %lu",
					 new_budget);
		bfq_activate_bfqq(bfqd, bfqq);
	}
}

static unsigned int bfq_wr_duration(struct bfq_data *bfqd)
{
	u64 dur;

	if (bfqd->bfq_wr_max_time > 0)
		return bfqd->bfq_wr_max_time;

	dur = bfqd->RT_prod;
	do_div(dur, bfqd->peak_rate);

	return dur;
}

/* Empty burst list and add just bfqq (see comments to bfq_handle_burst) */
static void bfq_reset_burst_list(struct bfq_data *bfqd, struct bfq_queue *bfqq)
{
	struct bfq_queue *item;
	struct hlist_node *n;

	hlist_for_each_entry_safe(item, n, &bfqd->burst_list, burst_list_node)
		hlist_del_init(&item->burst_list_node);
	hlist_add_head(&bfqq->burst_list_node, &bfqd->burst_list);
	bfqd->burst_size = 1;
}

/* Add bfqq to the list of queues in current burst (see bfq_handle_burst) */
static void bfq_add_to_burst(struct bfq_data *bfqd, struct bfq_queue *bfqq)
{
	/* Increment burst size to take into account also bfqq */
	bfqd->burst_size++;

	if (bfqd->burst_size == bfqd->bfq_large_burst_thresh) {
		struct bfq_queue *pos, *bfqq_item;
		struct hlist_node *n;

		/*
		 * Enough queues have been activated shortly after each
		 * other to consider this burst as large.
		 */
		bfqd->large_burst = true;

		/*
		 * We can now mark all queues in the burst list as
		 * belonging to a large burst.
		 */
		hlist_for_each_entry(bfqq_item, &bfqd->burst_list,
				     burst_list_node)
		        bfq_mark_bfqq_in_large_burst(bfqq_item);
		bfq_mark_bfqq_in_large_burst(bfqq);

		/*
		 * From now on, and until the current burst finishes, any
		 * new queue being activated shortly after the last queue
		 * was inserted in the burst can be immediately marked as
		 * belonging to a large burst. So the burst list is not
		 * needed any more. Remove it.
		 */
		hlist_for_each_entry_safe(pos, n, &bfqd->burst_list,
					  burst_list_node)
			hlist_del_init(&pos->burst_list_node);
	} else /* burst not yet large: add bfqq to the burst list */
		hlist_add_head(&bfqq->burst_list_node, &bfqd->burst_list);
}

/*
 * If many queues happen to become active shortly after each other, then,
 * to help the processes associated to these queues get their job done as
 * soon as possible, it is usually better to not grant either weight-raising
 * or device idling to these queues. In this comment we describe, firstly,
 * the reasons why this fact holds, and, secondly, the next function, which
 * implements the main steps needed to properly mark these queues so that
 * they can then be treated in a different way.
 *
 * As for the terminology, we say that a queue becomes active, i.e.,
 * switches from idle to backlogged, either when it is created (as a
 * consequence of the arrival of an I/O request), or, if already existing,
 * when a new request for the queue arrives while the queue is idle.
 * Bursts of activations, i.e., activations of different queues occurring
 * shortly after each other, are typically caused by services or applications
 * that spawn or reactivate many parallel threads/processes. Examples are
 * systemd during boot or git grep.
 *
 * These services or applications benefit mostly from a high throughput:
 * the quicker the requests of the activated queues are cumulatively served,
 * the sooner the target job of these queues gets completed. As a consequence,
 * weight-raising any of these queues, which also implies idling the device
 * for it, is almost always counterproductive: in most cases it just lowers
 * throughput.
 *
 * On the other hand, a burst of activations may be also caused by the start
 * of an application that does not consist in a lot of parallel I/O-bound
 * threads. In fact, with a complex application, the burst may be just a
 * consequence of the fact that several processes need to be executed to
 * start-up the application. To start an application as quickly as possible,
 * the best thing to do is to privilege the I/O related to the application
 * with respect to all other I/O. Therefore, the best strategy to start as
 * quickly as possible an application that causes a burst of activations is
 * to weight-raise all the queues activated during the burst. This is the
 * exact opposite of the best strategy for the other type of bursts.
 *
 * In the end, to take the best action for each of the two cases, the two
 * types of bursts need to be distinguished. Fortunately, this seems
 * relatively easy to do, by looking at the sizes of the bursts. In
 * particular, we found a threshold such that bursts with a larger size
 * than that threshold are apparently caused only by services or commands
 * such as systemd or git grep. For brevity, hereafter we call just 'large'
 * these bursts. BFQ *does not* weight-raise queues whose activations occur
 * in a large burst. In addition, for each of these queues BFQ performs or
 * does not perform idling depending on which choice boosts the throughput
 * most. The exact choice depends on the device and request pattern at
 * hand.
 *
 * Turning back to the next function, it implements all the steps needed
 * to detect the occurrence of a large burst and to properly mark all the
 * queues belonging to it (so that they can then be treated in a different
 * way). This goal is achieved by maintaining a special "burst list" that
 * holds, temporarily, the queues that belong to the burst in progress. The
 * list is then used to mark these queues as belonging to a large burst if
 * the burst does become large. The main steps are the following.
 *
 * . when the very first queue is activated, the queue is inserted into the
 *   list (as it could be the first queue in a possible burst)
 *
 * . if the current burst has not yet become large, and a queue Q that does
 *   not yet belong to the burst is activated shortly after the last time
 *   at which a new queue entered the burst list, then the function appends
 *   Q to the burst list
 *
 * . if, as a consequence of the previous step, the burst size reaches
 *   the large-burst threshold, then
 *
 *     . all the queues in the burst list are marked as belonging to a
 *       large burst
 *
 *     . the burst list is deleted; in fact, the burst list already served
 *       its purpose (keeping temporarily track of the queues in a burst,
 *       so as to be able to mark them as belonging to a large burst in the
 *       previous sub-step), and now is not needed any more
 *
 *     . the device enters a large-burst mode
 *
 * . if a queue Q that does not belong to the burst is activated while
 *   the device is in large-burst mode and shortly after the last time
 *   at which a queue either entered the burst list or was marked as
 *   belonging to the current large burst, then Q is immediately marked
 *   as belonging to a large burst.
 *
 * . if a queue Q that does not belong to the burst is activated a while
 *   later, i.e., not shortly after, than the last time at which a queue
 *   either entered the burst list or was marked as belonging to the
 *   current large burst, then the current burst is deemed as finished and:
 *
 *        . the large-burst mode is reset if set
 *
 *        . the burst list is emptied
 *
 *        . Q is inserted in the burst list, as Q may be the first queue
 *          in a possible new burst (then the burst list contains just Q
 *          after this step).
 */
static void bfq_handle_burst(struct bfq_data *bfqd, struct bfq_queue *bfqq,
			     bool idle_for_long_time)
{
	/*
	 * If bfqq happened to be activated in a burst, but has been idle
	 * for at least as long as an interactive queue, then we assume
	 * that, in the overall I/O initiated in the burst, the I/O
	 * associated to bfqq is finished. So bfqq does not need to be
	 * treated as a queue belonging to a burst anymore. Accordingly,
	 * we reset bfqq's in_large_burst flag if set, and remove bfqq
	 * from the burst list if it's there. We do not decrement instead
	 * burst_size, because the fact that bfqq does not need to belong
	 * to the burst list any more does not invalidate the fact that
	 * bfqq may have been activated during the current burst.
	 */
	if (idle_for_long_time) {
		hlist_del_init(&bfqq->burst_list_node);
		bfq_clear_bfqq_in_large_burst(bfqq);
	}

	/*
	 * If bfqq is already in the burst list or is part of a large
	 * burst, then there is nothing else to do.
	 */
	if (!hlist_unhashed(&bfqq->burst_list_node) ||
	    bfq_bfqq_in_large_burst(bfqq))
		return;

	/*
	 * If bfqq's activation happens late enough, then the current
	 * burst is finished, and related data structures must be reset.
	 *
	 * In this respect, consider the special case where bfqq is the very
	 * first queue being activated. In this case, last_ins_in_burst is
	 * not yet significant when we get here. But it is easy to verify
	 * that, whether or not the following condition is true, bfqq will
	 * end up being inserted into the burst list. In particular the
	 * list will happen to contain only bfqq. And this is exactly what
	 * has to happen, as bfqq may be the first queue in a possible
	 * burst.
	 */
	if (time_is_before_jiffies(bfqd->last_ins_in_burst +
	    bfqd->bfq_burst_interval)) {
		bfqd->large_burst = false;
		bfq_reset_burst_list(bfqd, bfqq);
		return;
	}

	/*
	 * If we get here, then bfqq is being activated shortly after the
	 * last queue. So, if the current burst is also large, we can mark
	 * bfqq as belonging to this large burst immediately.
	 */
	if (bfqd->large_burst) {
		bfq_mark_bfqq_in_large_burst(bfqq);
		return;
	}

	/*
	 * If we get here, then a large-burst state has not yet been
	 * reached, but bfqq is being activated shortly after the last
	 * queue. Then we add bfqq to the burst.
	 */
	bfq_add_to_burst(bfqd, bfqq);
}

static void bfq_add_request(struct request *rq)
{
	struct bfq_queue *bfqq = RQ_BFQQ(rq);
	struct bfq_entity *entity = &bfqq->entity;
	struct bfq_data *bfqd = bfqq->bfqd;
	struct request *next_rq, *prev;
	unsigned long old_wr_coeff = bfqq->wr_coeff;
	bool interactive = false;

	bfq_log_bfqq(bfqd, bfqq, "add_request %d", rq_is_sync(rq));
	bfqq->queued[rq_is_sync(rq)]++;
	bfqd->queued++;

	elv_rb_add(&bfqq->sort_list, rq);

	/*
	 * Check if this request is a better next-serve candidate.
	 */
	prev = bfqq->next_rq;
	next_rq = bfq_choose_req(bfqd, bfqq->next_rq, rq, bfqd->last_position);
	BUG_ON(!next_rq);
	bfqq->next_rq = next_rq;

	if (!bfq_bfqq_busy(bfqq)) {
		bool soft_rt, in_burst,
		     idle_for_long_time = time_is_before_jiffies(
						bfqq->budget_timeout +
						bfqd->bfq_wr_min_idle_time);

#ifdef CONFIG_BFQ_GROUP_IOSCHED
		bfqg_stats_update_io_add(bfqq_group(RQ_BFQQ(rq)), bfqq,
					 rq->cmd_flags);
#endif
		if (bfq_bfqq_sync(bfqq)) {
			bool already_in_burst =
			   !hlist_unhashed(&bfqq->burst_list_node) ||
			   bfq_bfqq_in_large_burst(bfqq);
			bfq_handle_burst(bfqd, bfqq, idle_for_long_time);
			/*
			 * If bfqq was not already in the current burst,
			 * then, at this point, bfqq either has been
			 * added to the current burst or has caused the
			 * current burst to terminate. In particular, in
			 * the second case, bfqq has become the first
			 * queue in a possible new burst.
			 * In both cases last_ins_in_burst needs to be
			 * moved forward.
			 */
			if (!already_in_burst)
				bfqd->last_ins_in_burst = jiffies;
		}

		in_burst = bfq_bfqq_in_large_burst(bfqq);
		soft_rt = bfqd->bfq_wr_max_softrt_rate > 0 &&
			!in_burst &&
			time_is_before_jiffies(bfqq->soft_rt_next_start);
		interactive = !in_burst && idle_for_long_time;
		entity->budget = max_t(unsigned long, bfqq->max_budget,
				       bfq_serv_to_charge(next_rq, bfqq));

		if (!bfq_bfqq_IO_bound(bfqq)) {
			if (time_before(jiffies,
					RQ_BIC(rq)->ttime.last_end_request +
					bfqd->bfq_slice_idle)) {
				bfqq->requests_within_timer++;
				if (bfqq->requests_within_timer >=
				    bfqd->bfq_requests_within_timer)
					bfq_mark_bfqq_IO_bound(bfqq);
			} else
				bfqq->requests_within_timer = 0;
		}

		if (!bfqd->low_latency)
			goto add_bfqq_busy;

		/*
		 * If the queue:
		 * - is not being boosted,
		 * - has been idle for enough time,
		 * - is not a sync queue or is linked to a bfq_io_cq (it is
		 *   shared "for its nature" or it is not shared and its
		 *   requests have not been redirected to a shared queue)
		 * start a weight-raising period.
		 */
		if (old_wr_coeff == 1 && (interactive || soft_rt) &&
		    (!bfq_bfqq_sync(bfqq) || bfqq->bic)) {
			bfqq->wr_coeff = bfqd->bfq_wr_coeff;
			if (interactive)
				bfqq->wr_cur_max_time = bfq_wr_duration(bfqd);
			else
				bfqq->wr_cur_max_time =
					bfqd->bfq_wr_rt_max_time;
			bfq_log_bfqq(bfqd, bfqq,
				     "wrais starting at %lu, rais_max_time %u",
				     jiffies,
				     jiffies_to_msecs(bfqq->wr_cur_max_time));
		} else if (old_wr_coeff > 1) {
			if (interactive)
				bfqq->wr_cur_max_time = bfq_wr_duration(bfqd);
			else if (in_burst ||
				 (bfqq->wr_cur_max_time ==
				  bfqd->bfq_wr_rt_max_time &&
				  !soft_rt)) {
				bfqq->wr_coeff = 1;
				bfq_log_bfqq(bfqd, bfqq,
					"wrais ending at %lu, rais_max_time %u",
					jiffies,
					jiffies_to_msecs(bfqq->
						wr_cur_max_time));
			} else if (time_before(
					bfqq->last_wr_start_finish +
					bfqq->wr_cur_max_time,
					jiffies +
					bfqd->bfq_wr_rt_max_time) &&
				   soft_rt) {
				/*
				 *
				 * The remaining weight-raising time is lower
				 * than bfqd->bfq_wr_rt_max_time, which means
				 * that the application is enjoying weight
				 * raising either because deemed soft-rt in
				 * the near past, or because deemed interactive
				 * a long ago.
				 * In both cases, resetting now the current
				 * remaining weight-raising time for the
				 * application to the weight-raising duration
				 * for soft rt applications would not cause any
				 * latency increase for the application (as the
				 * new duration would be higher than the
				 * remaining time).
				 *
				 * In addition, the application is now meeting
				 * the requirements for being deemed soft rt.
				 * In the end we can correctly and safely
				 * (re)charge the weight-raising duration for
				 * the application with the weight-raising
				 * duration for soft rt applications.
				 *
				 * In particular, doing this recharge now, i.e.,
				 * before the weight-raising period for the
				 * application finishes, reduces the probability
				 * of the following negative scenario:
				 * 1) the weight of a soft rt application is
				 *    raised at startup (as for any newly
				 *    created application),
				 * 2) since the application is not interactive,
				 *    at a certain time weight-raising is
				 *    stopped for the application,
				 * 3) at that time the application happens to
				 *    still have pending requests, and hence
				 *    is destined to not have a chance to be
				 *    deemed soft rt before these requests are
				 *    completed (see the comments to the
				 *    function bfq_bfqq_softrt_next_start()
				 *    for details on soft rt detection),
				 * 4) these pending requests experience a high
				 *    latency because the application is not
				 *    weight-raised while they are pending.
				 */
				bfqq->last_wr_start_finish = jiffies;
				bfqq->wr_cur_max_time =
					bfqd->bfq_wr_rt_max_time;
			}
		}
		if (old_wr_coeff != bfqq->wr_coeff)
			entity->prio_changed = 1;
add_bfqq_busy:
		bfqq->last_idle_bklogged = jiffies;
		bfqq->service_from_backlogged = 0;
		bfq_clear_bfqq_softrt_update(bfqq);
		bfq_add_bfqq_busy(bfqd, bfqq);
	} else {
		if (bfqd->low_latency && old_wr_coeff == 1 && !rq_is_sync(rq) &&
		    time_is_before_jiffies(
				bfqq->last_wr_start_finish +
				bfqd->bfq_wr_min_inter_arr_async)) {
			bfqq->wr_coeff = bfqd->bfq_wr_coeff;
			bfqq->wr_cur_max_time = bfq_wr_duration(bfqd);

			bfqd->wr_busy_queues++;
			entity->prio_changed = 1;
			bfq_log_bfqq(bfqd, bfqq,
			    "non-idle wrais starting at %lu, rais_max_time %u",
			    jiffies,
			    jiffies_to_msecs(bfqq->wr_cur_max_time));
		}
		if (prev != bfqq->next_rq)
			bfq_updated_next_req(bfqd, bfqq);
