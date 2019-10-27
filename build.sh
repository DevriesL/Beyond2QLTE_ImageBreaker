#!/bin/bash

mkdir out

BUILD_CROSS_COMPILE=/home/devries/Workspace/toolchains/aarch64-linux-android-4.9/bin/aarch64-linux-android-
KERNEL_LLVM_BIN=/home/devries/Workspace/toolchains/snapdragon-llvm-6.0.9-linux64/toolchains/llvm-Snapdragon_LLVM_for_Android_6.0/prebuilt/linux-x86_64/bin/clang
CLANG_TRIPLE=aarch64-linux-gnu-
KERNEL_MAKE_ENV="DTC_EXT=$(pwd)/scripts/dtc/dtc CONFIG_BUILD_ARM64_DT_OVERLAY=y ANDROID_VERSION=90000"

make -j8 -C $(pwd) O=$(pwd)/out $KERNEL_MAKE_ENV ARCH=arm64 CROSS_COMPILE=$BUILD_CROSS_COMPILE REAL_CC=$KERNEL_LLVM_BIN CLANG_TRIPLE=$CLANG_TRIPLE beyond2qlte_chn_open_defconfig
make -j8 -C $(pwd) O=$(pwd)/out $KERNEL_MAKE_ENV ARCH=arm64 CROSS_COMPILE=$BUILD_CROSS_COMPILE REAL_CC=$KERNEL_LLVM_BIN CLANG_TRIPLE=$CLANG_TRIPLE | tee build.txt
 
cp out/arch/arm64/boot/Image-dtb $(pwd)/boot.img-zImage
