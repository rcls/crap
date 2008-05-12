#!/bin/sh

set -e -x

rm -rf t

mkdir t

cd t

git init
../rlog_parse "$@" | git-fast-import

