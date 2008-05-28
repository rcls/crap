#!/bin/sh
git-log  --pretty=format:'%T%n%ad %an %ae%n%s%n%b' "$@"
