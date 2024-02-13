#!/bin/bash
# Exports
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
}

build_kernel(){
	make O=out ARCH=arm64 -j$(nproc --all)
}

create_zip(){
    COUNT="counter.txt"
    # Check if the number file exists, if not, initialize it with 1
    if [ ! -f "$COUNT" ]; then
        echo "1" > "$COUNT"
    fi
    # Read the current number from the file and increment it
    CURRENT_NUMBER=$(<"$COUNT")
    NEXT_NUMBER=$((CURRENT_NUMBER + 1))
    echo "$NEXT_NUMBER" > "$COUNT"
    ZIP_FILENAME="Kernel_A40_${NEXT_NUMBER}.zip"
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
    curl -s -F chat_id="${CHAT_ID}" -F document=@"$SRCTREE/kernel_zip/anykernel/${ZIP_FILENAME}" "${TELEGRAM_API}"
}
