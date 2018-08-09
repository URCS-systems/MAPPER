#!/bin/sh

grep "runtime:" | sed "s/^.*runtime:\\s\\+\\?\\([0-9.]\\+\\) s.*$/\\1/"
