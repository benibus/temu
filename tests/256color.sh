#!/bin/sh

for y in `seq 0 15`; do
    for x in `seq 0 15`; do
        index=$((y*15+x))
        printf '\033[00;48;5;%dm \033[m' $index $index
    done
    printf '\n'
done
