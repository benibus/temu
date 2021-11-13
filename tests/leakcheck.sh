#!/bin/sh

if [ ! $(command -v valgrind) ]; then
    echo "${0##*/}: Could not find valgrind executable" >&2
    exit 1
fi

valgrind -s --leak-check=full "$@"

