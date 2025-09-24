# wasmc
The project was developed and tested using a GNU/Linux Debian 12 stable machine.

The following Debian 12 stable package are required:
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
git clone git://c9x.me/qbe.git
cd qbe
make
cd ..
mv qbe/qbe .
```

## Build from source
```
./build.sh
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
./compile.sh test/qsort.wasm
```
