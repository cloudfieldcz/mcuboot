#/bin/sh

# arguments
options=$(getopt -o ''  --long build: -- "$@")
[ $? -eq 0 ] || {
    echo "Incorrect options provided"
    exit 1
}
eval set -- "$options"
while true; do
    case "$1" in
    --build)
        shift;
        BUILD_TYPE_ARG=$1
        ;;
    --)
        shift
        break
        ;;
    esac
    shift
done

if [ -z "$BUILD_TYPE_ARG" ]; then
    echo "BUILD_TYPE_ARG is empty"
else
    echo "BUILD_TYPE_ARG is $BUILD_TYPE_ARG"
    BUILD_TYPE=$BUILD_TYPE_ARG
fi

# enviroment variables
if [ -z "$ZEPHYR_BASE" ]; then
    echo "ZEPHYR_BASE not set correctly"
    exit 1
fi
echo "ZEPHYR_BASE:'$ZEPHYR_BASE'"

[[ ! $BUILD_TYPE =~ DEVEL|TEST|PROD ]] && {
    echo "Incorrect options provided for build. Use DEVEL|TEST|PROD."
    exit 1
}

if [ -z "$BUILD_TYPE" ]; then
    echo "BUILD_TYPE is empty"
    exit 1
fi

###
start_dir=`pwd`
echo "start_dir:"$start_dir

script_dir="$(dirname "$0")"
echo "script_dir:"$script_dir

# generovani
cd $script_dir

build_dir=build_bl_$BUILD_TYPE
echo "Vytvarim:"$build_dir

mkdir -p $build_dir
rm -rf $build_dir/*

#west build -b snbc_ecb -- -DBOARD_ROOT=$ZEPHYR_BASE/../zetbox/prj/ecb

cmake -B $build_dir -DBOARD=snbc_ecb -DBOARD_ROOT=$ZEPHYR_BASE/../bootloader/mcuboot/boot/zephyr -DBUILD_TYPE=$BUILD_TYPE -G'CodeLite - Ninja'
# cp .clang-format $build_dir

# preklad
cd $build_dir
ninja
