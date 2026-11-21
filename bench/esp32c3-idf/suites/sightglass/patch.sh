#!/bin/sh

set -e
cd "$(dirname "$0")"

git submodule update --init upstream
git -C upstream diff --quiet || { echo "upstream/ is modified"; exit 1; }

SRC=../build/sightglass
rm -rf "$SRC"
mkdir -p "$SRC"
cp upstream/benchmarks/shootout/*.c "$SRC"

for p in patches/*.patch; do
    patch --quiet --strip=3 --directory "$SRC" < "$p"
done
