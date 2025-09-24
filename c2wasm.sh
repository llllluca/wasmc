#!/bin/sh

dir=$(dirname $1)
name=$(basename $1)
name=$(echo $name | cut -d '.' -f 1)
clang \
    --target=wasm32 \
    -nostdlib \
    -Wl,--no-entry \
    -Wl,--export-all \
    -o $dir/$name.wasm $dir/$name.c

