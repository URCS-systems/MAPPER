#!/bin/sh

grep "QUERY TIME" | sed "s/^.*QUERY TIME: \\?\\([0-9.]\\+\\) seconds.*$/\\1/"
