#!/bin/sh

gcc -ggdb -std=c99 -Wall -Wextra -Wpedantic -Isrc src/*.c -o main
