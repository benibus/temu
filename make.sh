#!/bin/sh

CMAKE=`command -v cmake`
DIR_ROOT=`readlink -e .`
DIR_BUILD=${DIR_ROOT}/build

o_clean=0
o_delete=0
o_build_type=""
o_build_flags=""

usage()
{
	echo "Usage: ${0} [options] [target]"
	echo "Options:"
	echo "  -h, --help      Print usage and exit."
	echo "  -v, --verbose   Enable verbose compiler output."
	echo "Targets:"
	echo "  debug           Build CMake cache + executable for debug version (default)"
	echo "  release         Build CMake cache + executable for release version"
	echo "  clean           Clean objects in build directory, then build target (only if specified)"
	echo "  delete          Delete entire build directory and exit"
	echo ""
	echo "If no target is specified, the executable is rebuilt using the existing CMake cache."
	echo "If no such cache exists, the default cache/build directory is created first."
}

for arg in "$@"; do
    case "$arg" in
        ("-h"|"--help") usage; exit 0 ;;
        ("-v"|"--verbose") o_build_flags="--verbose" ;;
        ("debug")   o_build_type="Debug"   ;;
        ("release") o_build_type="Release" ;;
        ("clean")   o_clean=1  ;;
        ("delete")  o_delete=1 ;;
    esac
done

# Delete build directory
if [ $o_delete -ne 0 ]; then
    echo "Deleting `readlink -e ${DIR_BUILD}`..."
    rm -rfv "$DIR_BUILD"
    exit 0
fi

# Create build directory and generate cmake cache
if [ -n "$o_build_type" ] || [ ! -d "$DIR_BUILD" ]; then
    mkdir -pv "$DIR_BUILD"
    [ -z "$o_build_type" ] && str="Debug" || str="$o_build_type"
    command ${CMAKE} -S "$DIR_ROOT" -B "$DIR_BUILD" -DCMAKE_BUILD_TYPE=${str} || \
        { echo "Failed to build cmake cache... exiting."; exit 1; }
fi

# Clean build directory
if [ $o_clean -ne 0 ] && [ -d "$DIR_BUILD" ]; then
    echo "Cleaning `readlink -e ${DIR_BUILD}`..."
    command ${CMAKE} --build "$DIR_BUILD" --target clean
    [ -z "$o_build_type" ] && exit 0
fi

# Build application
command ${CMAKE} --build "$DIR_BUILD" ${o_build_flags}

