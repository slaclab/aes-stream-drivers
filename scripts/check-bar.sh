#!/usr/bin/env bash

# Quickly prints out the BAR assignments for all SLAC cards on the system
for d in $(lspci -d 1a4a: | grep -Eo '^\S+')
do
    echo "$d: $(lspci -s "$d" -vvv | grep -Eo 'Memory at \w+')"
done
