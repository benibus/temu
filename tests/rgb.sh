#!/bin/sh

for y in `seq 0 15`; do
	for x in `seq 0 15`; do
		r=$((y*4+x/4+16))
		g=$((y*8+x/2+8))
		b=$((y*16+x))
		printf '\033[00;48;2;%d;%d;%dm \033[m' $r $g $b
	done
	printf '\n'
done
