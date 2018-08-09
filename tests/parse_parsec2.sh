#!/bin/sh

grep "real" | sed "s/^.*real.*\\([[:digit:]]\\+\\)m\\([0-9.]\\+\\).*$/\\1\t\\2/" | awk '{ printf "%f", $1 * 60 + $2 }'
