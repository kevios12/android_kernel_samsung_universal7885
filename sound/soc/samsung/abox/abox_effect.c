/* sound/soc/samsung/abox/abox_effect.c
 *
 * ALSA SoC Audio Layer - Samsung Abox Effect driver
 *
 * Copyright (c) 2016 Samsung Electronics Co. Ltd.
  *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
//#define DEBUG

#include <sound/soc.h>
#include <sound/tlv.h>

#include <linux/module.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>
#include <sound/samsung/abox.h>

#include "abox.h"
#include "abox_util.h"
#include "abox_effect.h"

#include <linux/stub_logs.h>

struct abox_ctl_eq_switch {
	unsigned int base;
	unsigned int count;
	unsigned int min;
	unsigned int max;
};

static int abox_ctl_info(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_info *uinfo)
{
	struct abox_ctl_eq_switch *params = (void *)kcontrol->private_value;

	dev_dbg(&pdev->dev, "%s: %s\n", __func__, kcontrol->id.name);

	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = params->count;
	uinfo->value.integer.min = params->min;
	uinfo->value.integer.max = params->max;
	return 0;
}

static int abox_ctl_get(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_soc_kcontrol_codec(kcontrol);
	struct device *dev = codec->dev;
	struct snd_soc_component *component = snd_kcontrol_chip(kcontrol);
	struct abox_ctl_eq_switch *params = (void *)kcontrol->private_value;
	int i;

	dev_dbg(dev, "%s: %s\n", __func__, kcontrol->id.name);

	pm_runtime_get_sync(dev);
	for (i = 0; i < params->count; i++) {
		unsigned int val;

		snd_soc_component_read(component, params->base + PARAM_OFFSET + (i * sizeof(u32)), &val);
		ucontrol->value.integer.value[i] = val;
		dev_dbg(dev, "%s[%d] = %u\n", kcontrol->id.name, i, val);
	}
	pm_runtime_put_autosuspend(dev);

	return 0;
}

static int abox_ctl_put(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_soc_kcontrol_codec(kcontrol);
	struct device *dev = codec->dev;
	struct snd_soc_component *component = snd_kcontrol_chip(kcontrol);
	struct abox_ctl_eq_switch *params = (void *)kcontrol->private_value;
	int i;

	dev_dbg(dev, "%s: %s\n", __func__, kcontrol->id.name);

	pm_runtime_get_sync(dev);
	for (i = 0; i < params->count; i++) {
		unsigned int val = ucontrol->value.integer.value[i];

		snd_soc_component_write(component, params->base + PARAM_OFFSET + (i * sizeof(u32)), val);
		dev_dbg(dev, "%s[%d] <= %u\n", kcontrol->id.name, i, val);
	}
	snd_soc_component_write(component, params->base, CHANGE_BIT);
	pm_runtime_put_autosuspend(dev);

	return 0;
}

#define ABOX_CTL_EQ_SWITCH(xname, xbase, xcount, xmin, xmax)	\
{	.iface = SNDRV_CTL_ELEM_IFACE_MIXER, .name = (xname),	\
	.access = SNDRV_CTL_ELEM_ACCESS_READWRITE,		\
	.info = abox_ctl_info, .get = abox_ctl_get,		\
	.put = abox_ctl_put, .private_value =			\
		((unsigned long)&(struct abox_ctl_eq_switch)	\
		{.base = xbase, .count = xcount,		\
		.min = xmin, .max = xmax}) }

#define DECLARE_ABOX_CTL_EQ_SWITCH(name, prefix)	\
		ABOX_CTL_EQ_SWITCH(name, prefix##_BASE,	\
		prefix##_MAX_COUNT, prefix##_VALUE_MIN,	\
		prefix##_VALUE_MAX)

static const struct snd_kcontrol_new abox_effect_controls[] = {
	DECLARE_ABOX_CTL_EQ_SWITCH("SA data", SA),
	DECLARE_ABOX_CTL_EQ_SWITCH("Audio DHA data", MYSOUND),
	DECLARE_ABOX_CTL_EQ_SWITCH("VSP data", VSP),
	DECLARE_ABOX_CTL_EQ_SWITCH("LRSM data", LRSM),
	DECLARE_ABOX_CTL_EQ_SWITCH("MSP data", MYSPACE),
	DECLARE_ABOX_CTL_EQ_SWITCH("ESA BBoost data", BB),
	DECLARE_ABOX_CTL_EQ_SWITCH("ESA EQ data", EQ),
	DECLARE_ABOX_CTL_EQ_SWITCH("NXP BDL data", NXPBDL),
	DECLARE_ABOX_CTL_EQ_SWITCH("NXP RVB ctx data", NXPRVB_CTX),
	DECLARE_ABOX_CTL_EQ_SWITCH("NXP RVB param data", NXPRVB_PARAM),
};

#define ABOX_EFFECT_ACCESSIABLE_REG_CONDITION(name, reg) \
		(name##_BASE <= reg && reg <= name##_BASE + PARAM_OFFSET + (name##_MAX_COUNT * sizeof(u32)))

static bool abox_effect_accessible_reg(struct device *dev, unsigned int reg) {
	return ABOX_EFFECT_ACCESSIABLE_REG_CONDITION(SA, reg)			||
			ABOX_EFFECT_ACCESSIABLE_REG_CONDITION(MYSOUND, reg)	||
			ABOX_EFFECT_ACCESSIABLE_REG_CONDITION(VSP, reg)		||
			ABOX_EFFECT_ACCESSIABLE_REG_CONDITION(LRSM, reg)	||
			ABOX_EFFECT_ACCESSIABLE_REG_CONDITION(MYSPACE, reg)	||
			ABOX_EFFECT_ACCESSIABLE_REG_CONDITION(BB, reg)		||
			ABOX_EFFECT_ACCESSIABLE_REG_CONDITION(EQ, reg)		||
			ABOX_EFFECT_ACCESSIABLE_REG_CONDITION(ELPE, reg)	||
			ABOX_EFFECT_ACCESSIABLE_REG_CONDITION(NXPBDL, reg)	||
			ABOX_EFFECT_ACCESSIABLE_REG_CONDITION(NXPRVB_CTX, reg)	||
			ABOX_EFFECT_ACCESSIABLE_REG_CONDITION(NXPRVB_PARAM, reg);
}

#define ABOX_EFFECT_VOLATILE_REG_CONDITION(name, reg) (name##_BASE == reg)

static bool abox_effect_volatile_reg(struct device *dev, unsigned int reg) {
	return ABOX_EFFECT_VOLATILE_REG_CONDITION(SA, reg)			||
			ABOX_EFFECT_VOLATILE_REG_CONDITION(MYSOUND, reg)	||
			ABOX_EFFECT_VOLATILE_REG_CONDITION(VSP, reg)		||
			ABOX_EFFECT_VOLATILE_REG_CONDITION(LRSM, reg)		||
			ABOX_EFFECT_VOLATILE_REG_CONDITION(MYSPACE, reg)	||
			ABOX_EFFECT_VOLATILE_REG_CONDITION(BB, reg)		||
			ABOX_EFFECT_VOLATILE_REG_CONDITION(EQ, reg)		||
			ABOX_EFFECT_VOLATILE_REG_CONDITION(ELPE, reg)		||
			ABOX_EFFECT_VOLATILE_REG_CONDITION(NXPBDL, reg)		||
			ABOX_EFFECT_VOLATILE_REG_CONDITION(NXPRVB_CTX, reg)	||
			ABOX_EFFECT_VOLATILE_REG_CONDITION(NXPRVB_PARAM, reg);
}

static const struct regmap_config abox_effect_regmap_config = {
	.reg_bits		= 32,
	.val_bits		= 32,
	.reg_stride		= 4,
	.max_register		= ABOX_MAX_REGISTERS,
	.writeable_reg		= abox_effect_accessible_reg,
	.readable_reg		= abox_effect_accessible_reg,
	.volatile_reg		= abox_effect_volatile_reg,
	.cache_type		= REGCACHE_FLAT,
};

static struct regmap * abox_effect_get_regmap(struct device *dev)
{
	struct abox_effect_data *data = dev_get_drvdata(dev);

	return data->regmap;
}

static const struct snd_soc_codec_driver abox_effect = {
	.controls		= abox_effect_controls,
	.num_controls		= ARRAY_SIZE(abox_effect_controls),
	.get_regmap		= abox_effect_get_regmap,
	.idle_bias_off		= true,
	.suspend_bias_off	= true,
	.ignore_pmdown_time	= true,
};

static int abox_effect_runtime_suspend(struct device *dev)
{
	struct abox_effect_data *data = dev_get_drvdata(dev);

	dev_dbg(dev, "%s\n", __func__);

	regcache_cache_only(data->regmap, true);
	regcache_mark_dirty(data->regmap);

	return 0;
}

static int abox_effect_runtime_resume(struct device *dev)
{
	struct abox_effect_data *data = dev_get_drvdata(dev);

	dev_dbg(dev, "%s\n", __func__);

	regcache_cache_only(data->regmap, false);
	regcache_sync(data->regmap);

	return 0;
}

const struct dev_pm_ops samsung_abox_effect_pm = {
	SET_RUNTIME_PM_OPS(abox_effect_runtime_suspend, abox_effect_runtime_resume, NULL)
};

static int samsung_abox_effect_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
	struct device_node *np_tmp;
	struct abox_effect_data *data;

	dev_dbg(dev, "%s\n", __func__);

	data = devm_kzalloc(dev, sizeof(*data), GFP_KERNEL);
	if (data == NULL) {
		return -ENOMEM;
	}
	platform_set_drvdata(pdev, data);

	np_tmp = of_parse_phandle(np, "abox", 0);
	if (!np_tmp) {
		dev_err(dev, "Failed to get abox device node\n");
		return -EPROBE_DEFER;
	}
	data->pdev_abox = of_find_device_by_node(np_tmp);
	if (!data->pdev_abox) {
		dev_err(dev, "Failed to get abox platform device\n");
		return -EPROBE_DEFER;
	}

	data->pdev = pdev;

	data->base = devm_not_request_and_map(pdev, "reg", 0, NULL, NULL);
	if (IS_ERR(data->base)) {
		dev_err(dev, "base address request failed: %ld\n", PTR_ERR(data->base));
		return PTR_ERR(data->base);
	}

	data->regmap = devm_regmap_init_mmio(dev, data->base, &abox_effect_regmap_config);
	if (IS_ERR(data->regmap)) {
		dev_err(dev, "regmap init failed: %ld\n", PTR_ERR(data->regmap));
		return PTR_ERR(data->regmap);
	}

	pm_runtime_enable(dev);
	pm_runtime_set_autosuspend_delay(dev, 1000);
	pm_runtime_use_autosuspend(dev);

	return snd_soc_register_codec(&pdev->dev, &abox_effect, NULL, 0);
}

static int samsung_abox_effect_remove(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;

	dev_dbg(dev, "%s\n", __func__);

	pm_runtime_disable(dev);
	snd_soc_unregister_codec(&pdev->dev);

	return 0;
}

static const struct of_device_id samsung_abox_effect_match[] = {
	{
		.compatible = "samsung,abox-effect",
	},
	{},
};
MODULE_DEVICE_TABLE(of, samsung_abox_effect_match);

static struct platform_driver samsung_abox_effect_driver = {
	.probe  = samsung_abox_effect_probe,
	.remove = samsung_abox_effect_remove,
	.driver = {
		.name = "samsung-abox-effect",
		.owner = THIS_MODULE,
		.of_match_table = of_match_ptr(samsung_abox_effect_match),
		.pm = &samsung_abox_effect_pm,
	},
};

module_platform_driver(samsung_abox_effect_driver);

MODULE_AUTHOR("Gyeongtaek Lee, <gt82.lee@samsung.com>");
MODULE_DESCRIPTION("Samsung ASoC A-Box Effect Driver");
MODULE_ALIAS("platform:samsung-abox-effect");
MODULE_LICENSE("GPL");
