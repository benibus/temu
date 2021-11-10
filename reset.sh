#!/bin/sh

SUBDIR=build

cd $(dirname ${0}) || exit 1

if [ -d ${SUBDIR} ]; then
    echo "${0##*/}: Removing directory $(readlink -e ${SUBDIR})..."
    rm -Ir ${SUBDIR}
fi

