# wasmc
The project was developed and tested using a GNU/Linux Debian 13 stable machine.

The following Debian 13 stable package are required:
```
sudo apt install \
    git \
    gcc \
    clang \
    wabt
```

Also the `qbe` executable is required:
```
cd wasmc
git clone git://c9x.me/qbe.git qbe-git
cd qbe-git
make
cd ..
mv qbe-git/qbe .
```

## Build from source
```
make
```

## How to run tests
```
./run_test.sh
```

## How compile from C to WASM (using clang)
```
./c2wasm.sh test/qsort.c
```

## How compile from WASM to machine code
```
./wasmc test/qsort.wasm > test/qsort.ssa
./qbe test/qsort.ssa > test/qsort.s
cc -o qsort test/qsort.s
```

./wamrc --target=riscv32 --format=object --stack-bounds-checks=0 -o add.o test/add.wasm
./wamrc --target=riscv32 --format=aot --stack-bounds-checks=0 -o add.aot test/add.wasm
