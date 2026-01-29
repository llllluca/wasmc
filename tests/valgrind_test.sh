wasm_file="$1"
if [ ! -f "$wasm_file" ]; then
    echo "[ERROR]: the input file '${wasm_file}' doesn't exists." >&2
    exit 1
fi

root_dir=$(git rev-parse --show-toplevel)
apps_dir=${root_dir}/build/apps

valgrind --error-exitcode=1 \
        --tool=memcheck \
        --leak-check=full \
        --track-origins=yes \
        ${apps_dir}/wasmc ${wasm_file}

exit $?
