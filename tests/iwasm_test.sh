wasm_file="$1"
iwasm_args=$2

if [ ! -f "$wasm_file" ]; then
    echo "[ERROR]: the input file '${wasm_file}' doesn't exists." >&2
    exit 1
fi

root_dir=$(git rev-parse --show-toplevel)
apps_dir=${root_dir}/build/apps

${apps_dir}/wasmc "$wasm_file"
if [ "$?" != 0 ]; then
    echo "[ERROR]: wasmc fail." >&2
    exit 1
fi

iwasm-2.4.4 -f main a.aot ${iwasm_args}
if [ "$?" != 0 ]; then
    echo "[ERROR]: iwasm fail." >&2
    exit 1
fi

rm "a.aot"
exit 0

