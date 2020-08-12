#/bin/sh

if [ -z "$ZEPHYR_BASE" ]; then
echo "ZEPHYR_BASE not set correctly"
exit
fi

echo "ZEPHYR_BASE:'$ZEPHYR_BASE'"

start_dir=`pwd`
echo "start_dir:"$start_dir

script_dir="$(dirname "$0")"
echo "script_dir:"$script_dir

# generovani
cd $script_dir

build_dir=build_bl
echo "Vytvarim:"$build_dir

mkdir -p $build_dir
rm -rf $build_dir/*

#west build -b snbc_ecb -- -DBOARD_ROOT=$ZEPHYR_BASE/../zetbox/prj/ecb

cmake -B $build_dir -DBOARD=snbc_ecb -DBOARD_ROOT=$ZEPHYR_BASE/../bootloader/mcuboot/boot/zephyr -G'CodeLite - Ninja'
# cp .clang-format $build_dir

# preklad
cd $build_dir
ninja
