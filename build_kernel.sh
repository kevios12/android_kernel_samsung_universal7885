#!/bin/bash

# Export build variables
export KBUILD_BUILD_USER=""
export KBUILD_BUILD_HOST=""
export PLATFORM_VERSION=11
export ANDROID_MAJOR_VERSION=r

SRCTREE=$(pwd)

# Lets make the Terminal a bit more Colorful
GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[0;33m'
BLUE='\033[0;34m'
NC='\033[0m'

# Compiler
TOOLCHAIN="$SRCTREE/toolchain/bin"
LLVM_DIS_ARGS="llvm-dis AR=llvm-ar AS=llvm-as NM=llvm-nm LD=ld.lld OBJCOPY=llvm-objcopy OBJDUMP=llvm-objdump STRIP=llvm-strip"
TRIPLE="aarch64-linux-gnu-"
CROSS="$SRCTREE/toolchain/bin/aarch64-linux-gnu-"
CROSS_ARM32="$SRCTREE/toolchain/bin/arm-linux-gnueabi-"
LD="$SRCTREE/toolchain/lib"

# OS
DISTRO=$(lsb_release -d | awk -F"\t" '{print $2}')
GLIBC_VERSION=$(ldd --version | grep 'ldd (' | awk '{print $NF}')

# Telegram (Please Configure it or Press on end: 2(No)"
CHAT_ID=""
BOT_TOKEN=""
TELEGRAM_API="https://api.telegram.org/bot${BOT_TOKEN}"

# Initialize
init() {
	clear
	echo -e "${YELLOW}Warning: Your BOT Token or Chat ID is Empty!${NC}\n"
	sleep 2
	clear
	echo -e "${GREEN}Do u want to make Clean Build?${NC}\n"
	select clean in "Yes" "No" "Exit"; do
		case "$clean" in
		"Yes")
			echo -e "${YELLOW}Cleaning up ...${NC}\n"
			rm -rf out && make clean && make mrproper
			toolchain
			break
			;;
		"No")
			toolchain
			break
			;;
		"Exit")
			clear
			echo -e "${RED}Exiting ...${NC}\n"
			break
			;;
		*)
			echo -e "${RED}Invalid option. Please select again.${NC}\n"
			;;
		esac
	done
}

# Let's do a Toolchain check
toolchain() {
	clear
	if [ -d "$TOOLCHAIN" ]; then
		clear
		echo -e "${GREEN}Looks like you already have a Toolchain.${NC}\n"
		echo -e "${YELLOW}Do you want to continue and start the Build?${NC}\n"
		select choice in "Continue" "Exit"; do
			case "$choice" in
			"Continue")
				clear
				select_device
				break
				;;
			"Exit")
				clear
				echo -e "${RED}Exiting ...${NC}\n"
				exit
				;;
			*)
				echo -e "${RED}Invalid option. Please select again.${NC}\n"
				;;
			esac
		done
	else
		echo -e "${RED}No Toolchain found!${NC}\n"
		echo -e "${YELLOW}Warning: Toolchains need different Operating Systems!${NC}\n"
		echo -e "${YELLOW}Check below if your System are compatible before u Continue.${NC}\n"
		echo -e "${GREEN}Your current OS is: $DISTRO - GLIBC: $GLIBC_VERSION${NC}\n"
		if [[ "$DISTRO" == "Ubuntu 24.04 LTS" ]]; then
			echo -e "${YELLOW}➜ Neutron needs ${GREEN}Ubuntu 23.10/24.04 [GLIBC2.38]${NC}"
			echo -e "${YELLOW}Vortex needs ${RED}Ubuntu 21.10 [GLIBC2.34]${NC}"
			echo -e "${YELLOW}Proton needs ${RED}Ubuntu 20.04 [GLIBC2.31]${NC}"
		elif [[ "$DISTRO" == "Ubuntu 23.10" ]]; then
			echo -e "${YELLOW}➜ Neutron needs ${GREEN}Ubuntu 23.10 [GLIBC2.38]${NC}"
			echo -e "${YELLOW}Vortex needs ${RED}Ubuntu 21.10 [GLIBC2.34]${NC}"
			echo -e "${YELLOW}Proton needs ${RED}Ubuntu 20.04 [GLIBC2.31]${NC}"
		elif [[ "$DISTRO" == "Ubuntu 21.10" ]]; then
			echo -e "${YELLOW}Neutron needs ${RED}Ubuntu 23.10 [GLIBC2.38]${NC}"
			echo -e "${YELLOW}➜ Vortex needs ${GREEN}Ubuntu 21.10 [GLIBC2.34]${NC}"
			echo -e "${YELLOW}Proton needs ${RED}Ubuntu 20.04 [GLIBC2.31]${NC}"
		elif [[ "$DISTRO" == "Ubuntu 20.04.6 LTS" ]]; then
			echo -e "${YELLOW}Neutron needs ${RED}Ubuntu 23.10 [GLIBC2.38]${NC}"
			echo -e "${YELLOW}Vortex needs ${RED}Ubuntu 21.10 [GLIBC2.34]${NC}"
			echo -e "${YELLOW}➜ Proton needs ${GREEN}Ubuntu 20.04.6 LTS [GLIBC2.31]${NC}"
		fi
		echo ""
		echo -e "${GREEN}Proceed to Download the Toolchain?${NC}\n"
		select toolchain in "Yes" "Exit"; do
			case $toolchain in
			"Yes")
				if [[ "$DISTRO" == "Ubuntu 24.04 LTS" ]]; then
					echo -e "${YELLOW}Downloading Neutron-Clang Toolchain ...${NC}\n"
					mkdir -p "$HOME/toolchains/neutron-clang"
					cd "$HOME/toolchains/neutron-clang"
					bash <(curl -s "https://raw.githubusercontent.com/Neutron-Toolchains/antman/main/antman") -S
					cd "$SRCTREE"
					cp -r "$HOME/toolchains/neutron-clang" toolchain
					clear
					select_device
				elif [[ "$DISTRO" == "Ubuntu 23.10" ]]; then
					echo -e "${YELLOW}Downloading Neutron-Clang Toolchain ...${NC}\n"
					mkdir -p "$HOME/toolchains/neutron-clang"
					cd "$HOME/toolchains/neutron-clang"
					bash <(curl -s "https://raw.githubusercontent.com/Neutron-Toolchains/antman/main/antman") -S
					cd "$SRCTREE"
					cp -r "$HOME/toolchains/neutron-clang" toolchain
					clear
					select_device
				elif [[ "$DISTRO" == "Ubuntu 21.10" ]]; then
					echo -e "${YELLOW}Downloading Vortex-Clang Toolchain ...${NC}\n"
					git clone --depth=1 https://github.com/vijaymalav564/vortex-clang toolchain
					clear
					select_device
				elif [[ "$DISTRO" == "Ubuntu 20.04.6 LTS" ]]; then
					echo -e "${YELLOW}Downloading Proton-Clang Toolchain ...${NC}\n"
					git clone --depth=1 https://github.com/kdrag0n/proton-clang toolchain
					clear
					select_device
				fi
				;;
			"Exit")
				clear
				echo -e "${RED}Exiting ...${NC}\n"
				exit
				;;
			*)
				echo -e "${RED}Invalid option. Please select again.${NC}\n"
				;;
			esac
		done
	fi
}

select_device() {
	echo -e "${GREEN}Please select your Device${NC}\n"
	select devices in "Galaxy A40 (a40)" "Galaxy A8 2018 (jackpotlte)" "Exit"; do
		case "$devices" in
		"Galaxy A40 (a40)")
			codename="a40"
			echo -e "${BLUE}"
			make O=out ARCH=arm64 exynos7885-${codename}_defconfig
			echo -e "${NC}"
			build_kernel
			break
			;;
		"Galaxy A8 2018 (jackpotlte)")
			codename="jackpotlte"
			echo -e "${BLUE}"
			make O=out ARCH=arm64 exynos7885-${codename}_defconfig
			echo -e "${NC}"
			build_kernel
			break
			;;
		"Exit")
			clear
			echo -e "${RED}Exiting ...${NC}\n"
			exit
			;;
		*)
			echo -e "${RED}Invalid option. Please select again.${NC}\n"
			;;
		esac
	done
}

build_kernel() {
	echo -e "${BLUE}"
	PATH=$TOOLCHAIN:$PATH \
		make O=out -j$(nproc --all) \
		ARCH=arm64 \
		LLVM_DIS=$LLVM_DIS_ARGS \
		LLVM=1 \
		CC=clang \
		LD_LIBRARY_PATH="$LD:$LD_LIBRARY_PATH" \
		CLANG_TRIPLE=$TRIPLE \
		CROSS_COMPILE=$CROSS \
		CROSS_COMPILE_ARM32=$CROSS_ARM32 \
		&> compilelog.txt
	echo -e "${NC}"
	clear
	echo -e "${YELLOW}Creating ZIP for $codename ...${NC}\n"
	pack
}

create_zip() {
	cd $SRCTREE
	# Extract version information from Makefile
	VERSION=$(awk '/^VERSION/ {print $3}' Makefile)
	PATCHLEVEL=$(awk '/^PATCHLEVEL/ {print $3}' Makefile)
	SUBLEVEL=$(awk '/^SUBLEVEL/ {print $3}' Makefile)
	cd $SRCTREE/kernel_zip/anykernel/
	if ! grep -q "# Auto-Generated by" "version"; then
		sed -i '1i# Auto-Generated by '"$0"'!' "version"
	fi
	sed -i "s/Kernel: .*$/Kernel: $VERSION.$PATCHLEVEL.$SUBLEVEL/g" "version"
	sed -i "s/Build Date: .*/Build Date: $(date +'%Y-%m-%d %H:%M %Z')/g" "version"
	ZIP_FILENAME="Nameless_${codename}_v1.zip"
	zip -r9 "$ZIP_FILENAME" "$@"
}

pack() {
	# create and pack ZIP
	cp out/arch/arm64/boot/Image kernel_zip/anykernel
	cp out/arch/arm64/boot/dtb.img kernel_zip/anykernel
	cp out/arch/arm64/boot/dtbo.img kernel_zip/anykernel
	cd $SRCTREE/kernel_zip/anykernel/
	if [ "$codename" = "a40" ]; then
		create_zip META-INF tools anykernel.sh Image dtb.img dtbo.img version
	elif [ "$codename" = "jackpotlte" ]; then
		create_zip META-INF tools anykernel.sh Image dtb.img version
	fi
	cd $SRCTREE
	clear
	echo -e "${GREEN}Zip file created: $ZIP_FILENAME and saved in $SRCTREE/kernel_zip/anykernel/${NC}\n"
	echo -e "${GREEN}***************************************************"
	echo "	          Kernel: $VERSION.$PATCHLEVEL.$SUBLEVEL"
	echo "          Build Date: $(date +'%Y-%m-%d %H:%M %Z')"
	echo -e "***************************************************${NC}\n"
	echo -e "${GREEN}Upload to Telegram?${NC}\n"
	tg_upload
}

tg_upload() {
	select telegram in "Yes" "No"; do
		case "$telegram" in
		"Yes")
			echo -e "${GREEN}Uploading $SRCTREE/kernel_zip/anykernel/$ZIP_FILENAME to Telegram ...${NC}"
			file_size_mb=$(stat -c "%s" "$SRCTREE/kernel_zip/anykernel/${ZIP_FILENAME}" | awk '{printf "%.2f", $1 / (1024 * 1024)}')
			curl -s -X POST "${TELEGRAM_API}/sendMessage" -d "chat_id=${CHAT_ID}" -d "text=Uploading: ${ZIP_FILENAME}%0ASize: ${file_size_mb}MB%0ABuild Date: $(date +'%Y-%m-%d %H:%M %Z')"
			curl -s -F chat_id="${CHAT_ID}" -F document=@"$SRCTREE/kernel_zip/anykernel/${ZIP_FILENAME}" "${TELEGRAM_API}/sendDocument"
			clear
			break
			;;
		"No")
			clear
			echo -e "${RED}Telegram Upload skipped. Exiting ...${NC}\n"
			echo -e "${GREEN}ZIP Output: $SRCTREE/kernel_zip/anykernel/${ZIP_FILENAME}${NC}\n"
			exit
			;;
		*)
			echo -e "${RED}Invalid option. Please select again.${NC}\n"
			;;
		esac
	done
}

init
