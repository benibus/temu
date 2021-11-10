#!/bin/sh

SUBDIR=build

if [ ! $(command -v cmake) ]; then
    echo "${0##*/}: Could not find cmake executable"
    exit 1
fi

cd $(dirname ${0}) || exit 1

mkdir -pv ${SUBDIR} && cd ${SUBDIR} && command cmake .. "$@" || exit 1

