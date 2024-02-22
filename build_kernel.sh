#!/bin/bash
# Exports
export KBUILD_BUILD_USER="Kevios12"
export KBUILD_BUILD_HOST="ubuntu@kevios12"
export PLATFORM_VERSION=11
export ANDROID_MAJOR_VERSION=r
export ARCH=arm64
export O=out

SRCTREE=$(pwd)
declare -g ZIP_FILENAME

# Telegram
CHAT_ID=""
BOT_TOKEN=""
TELEGRAM_API="https://api.telegram.org/bot${BOT_TOKEN}/sendDocument"

make_defconfig(){
	make O=out ARCH=arm64 exynos7885-a40_defconfig
	build_kernel
}

build_kernel(){
	make O=out ARCH=arm64 -j$(nproc --all)
	upload
}

create_zip(){
    cd $SRCTREE
    # Extract version information from Makefile
    VERSION=$(awk '/^VERSION/ {print $3}' Makefile)
    PATCHLEVEL=$(awk '/^PATCHLEVEL/ {print $3}' Makefile)
    SUBLEVEL=$(awk '/^SUBLEVEL/ {print $3}' Makefile)
    COUNT="counter.txt"
    # Check if the number file exists, if not, initialize it with 1
    if [ ! -f "$COUNT" ]; then
        echo "1" > "$COUNT"
    fi
    # Read the current number from the file and increment it
    CURRENT_NUMBER=$(<"$COUNT")
    NEXT_NUMBER=$((CURRENT_NUMBER + 1))
    echo "$NEXT_NUMBER" > "$COUNT"
    cd $SRCTREE/kernel_zip/anykernel/
    sed -i "s/Kernel: .*$/Kernel: $VERSION.$PATCHLEVEL.$SUBLEVEL/g" "version"
    sed -i "s/Build Date: .*/Build Date: $(date +'%Y-%m-%d %H:%M')/g" "version"
    ZIP_FILENAME="Kernel_A40_${NEXT_NUMBER}_[$VERSION.$PATCHLEVEL.$SUBLEVEL].zip"
    zip -r9 "$ZIP_FILENAME" "$@"
    echo ""
    echo "Zip file created: $ZIP_FILENAME and saved in $SRCTREE/kernel_zip/anykernel/"
}

pack(){
    # create and pack ZIP
    cp out/arch/arm64/boot/Image kernel_zip/anykernel/
    cd $SRCTREE/kernel_zip/anykernel/
    create_zip META-INF tools anykernel.sh Image version
    cd $SRCTREE
}

upload(){
    pack
    echo ""
    echo "Uploading $SRCTREE/kernel_zip/anykernel/$ZIP_FILENAME to Telegram ..."
    # Upload to Telegram
    # curl -s -F chat_id="${CHAT_ID}" -F document=@"$SRCTREE/kernel_zip/anykernel/${ZIP_FILENAME}" "${TELEGRAM_API}"
}

make_defconfig
