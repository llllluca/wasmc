wasm_file="$1"
if [ ! -f "$wasm_file" ]; then
    echo "[ERROR]: the input file '${wasm_file}' doesn't exists." >&2
    exit 1
fi

root_dir=$(git rev-parse --show-toplevel)
apps_dir=${root_dir}/build/apps
suffix=".readaot.txt"
wamrc_readaot="wamrc${suffix}"
wasmc_readaot="wasmc${suffix}"


wamrc --target=riscv32 --format=aot -o "a.aot" "$wasm_file"
if [ "$?" != 0 ]; then
    echo "[ERROR]: wamrc fail." >&2
    exit 1
fi
${apps_dir}/readaot --target-info --init-data --export "a.aot" > "$wamrc_readaot"
if [ "$?" != 0 ]; then
    echo "[ERROR]: readaot fail (1)." >&2
    exit 1
fi


${apps_dir}/wasmc "$wasm_file"
if [ "$?" != 0 ]; then
    echo "[ERROR]: wasmc fail." >&2
    exit 1
fi
${apps_dir}/readaot --target-info --init-data --export "a.aot" > "$wasmc_readaot"
if [ "$?" != 0 ]; then
    echo "[ERROR]: readaot fail (2)." >&2
    exit 1
fi

diff --color=auto --unified "$wasmc_readaot" "$wamrc_readaot"
if [ "$?" != 0 ]; then
    exit 1
fi

rm "$wamrc_readaot" "$wasmc_readaot"
exit 0

