#!/bin/sh

set -e -x

rm -rf t

mkdir t

cd t

git init

date

#../crap-clone "$@" | git-fast-import --export-marks=marks.txt
#valgrind --child-silent-after-fork=yes --leak-check=full --show-reachable=yes ../crap-clone "$@"
valgrind --leak-check=full ../crap-clone "$@"

date
