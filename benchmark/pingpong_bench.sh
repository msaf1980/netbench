#!/usr/bin/env bash

PORT="9876"
BLOCK_SIZES="1 1024 4096 16384"
SESSIONS="1 1024 10240"
#DURATION="60"

while [ -n "$1" ]; do
    if [ -x "${1}" ]; then
        echo "$1"
        for block_size in ${BLOCK_SIZES}; do
            for session in ${SESSIONS}; do
                "${1}" -b ${block_size} -n ${session}
            done
        done
    else
        echo "${1} not found !" >&2
    fi

    shift
done
