#!/usr/bin/env bash

N=0

# Quickly prints out the BAR assignments for all SLAC cards on the system
for d in $(lspci -d 1a4a: | grep -Eo '^\S+')
do
    echo "$d: $(lspci -s "$d" -vvv | grep -Eo 'Memory at \w+')"
    N=$((N + 1))
done

echo "Total cards: $N"
