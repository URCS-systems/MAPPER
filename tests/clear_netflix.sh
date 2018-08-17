#!/bin/bash

items=()

for i in {1..6}; do
    items+=($(echo ~/netflix$i/* | xargs -n1 echo))
done

for f in ${items[@]}; do
    bn=`basename "$f"`
    if [[ "$bn" != "netflix_mm" ]] && [[ "$bn" != "netflix_mme" ]] && [[ "$bn" != "smallnetflix_mme" ]] && [[ "$bn" != "smallnetflix_mm" ]]; then
        rm -rf "$f"
    fi
done
