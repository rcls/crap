#!/bin/sh
git-log  --pretty=format:'%ad %an %ae%n%T%n%s%n%b' "$@"
