#!/bin/sh

set -e -x

rm -rf t

mkdir t

cd t

git init
../crap-clone "$@" | git-fast-import --export-marks=marks.txt
