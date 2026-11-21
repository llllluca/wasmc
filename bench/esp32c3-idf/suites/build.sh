#!/bin/sh

set -e
cd "$(dirname "$0")"

sightglass/compile.sh
printf "\n"
embench/compile.sh
