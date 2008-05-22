#!/bin/sh

set -e -x

rm -rf t

mkdir t

cd t

git init

date

valgrind --leak-check=full --show-reachable=yes ../crap-clone "$@" | git-fast-import --export-marks=marks.txt

date
