#!/bin/bash
# Usage: test.sh "<engine command>" /path/to/ed.wasm
# Runs ed sessions, with stdin from a pipe, and checks standard output, the
# files written and the exit code. The expected text follows V7 ed's rules.
engine=$1
wasm=$2
dir=$(mktemp -d)
trap 'rm -rf "$dir"' EXIT
cd "$dir" || exit 2
failed=0
total=0

# check NAME 'INPUT' 'EXPECTED OUTPUT' [ed arguments]: INPUT goes through printf %b
check() {
    local name=$1 input=$2 expected=$3
    shift 3
    total=$((total + 1))
    local actual rc
    actual=$(printf '%b' "$input" | $engine "$wasm" "$@" 2>&1; echo "rc=${PIPESTATUS[1]}")
    if [ "$actual" != "$expected"$'\n'"rc=0" ] && [ "$actual" != "${expected}rc=0" ]; then
        failed=$((failed + 1))
        echo "FAIL $name"
        diff <(printf '%s\n' "$expected" "rc=0") <(printf '%s\n' "$actual") | head -20
    fi
}

# same_file NAME FILE 'CONTENT': the file must hold CONTENT plus a newline
same_file() {
    total=$((total + 1))
    if [ "$(cat "$2")" != "$3" ]; then
        failed=$((failed + 1))
        echo "FAIL $1: $2 holds:"
        head -20 "$2"
    fi
}

check append_print 'a\none\ntwo\nthree\n.\n1,$p\n=\n2p\nQ\n' 'one
two
three
3
two'

check insert_change_delete 'a\na\nb\nc\n.\n2i\nx\n.\n1,$p\n3c\ny\n.\n1,$p\n1d\n1,$p\nQ\n' 'a
x
b
c
a
x
y
c
x
y
c'

# the longjmp replacement: errors unwind, the text survives, ed goes on
check errors_recover 'a\nkept\n.\nzz\n9p\n,p\ns/nothing/x/\n1p\nQ\n' '?
?
?
?
kept'

check error_inside_global 'a\nx1\nx2\n.\ng/x/s/nope/y/\n1,$p\nQ\n' 'x1
x2'

check error_many_times "a\nkept\n.\n$(for i in $(seq 3000); do printf 'zz\\n'; done)1p\nQ\n" "$(for i in $(seq 3000); do echo '?'; done)
kept"

# the error leaves global()'s frame on the C stack: without the reset of
# __stack_pointer in app_loop these overflow it
check error_stack_reset "a\nx\n.\n$(for i in $(seq 100); do printf 'g/x/zz\\n'; done)1p\nQ\n" "$(for i in $(seq 100); do echo '?'; done)
x"

check substitute 'a\nhello world\nfoo foo foo\n.\n1s/world/wasm/\np\n2s/foo/bar/\np\n2s/foo/baz/g\np\nQ\n' 'hello wasm
bar foo foo
bar baz baz'

check regex 'a\nabc123\nxyz\nabab\n.\n/[0-9]/p\n1s/\\([a-c]*\\)\\([0-9]*\\)/\\2\\1/p\n/^x.z$/p\n3s/\\(ab\\)\\1/<&>/p\n1;+1p\n?abc?p\nQ\n' 'abc123
123abc
xyz
<abab>
123abc
xyz
123abc'

check global 'a\nkeep 1\ndrop\nkeep 2\ndrop\n.\ng/drop/d\n1,$p\nv/1/s/keep/KEEP/\n1,$p\ng/e/p\nQ\n' 'keep 1
keep 2
keep 1
KEEP 2
keep 1'

check move_copy_join 'a\n1\n2\n3\n4\n.\n1m3\n1,$p\n1t$\n1,$p\n1,2j\n1,$p\nQ\n' '2
3
1
4
2
3
1
4
2
23
1
4
2'

check marks_undo 'a\nfirst\nsecond\n.\n1ka\n$\n'"'"'ap\n1s/first/changed/p\nu\n1p\nQ\n' 'second
first
changed
first'

check list 'a\na\tb\n.\nl\nQ\n' 'a-'$'\b''>b'

check write_read 'a\nalpha\nbeta\n.\nw out.txt\nq\n' '11'
same_file write_read out.txt 'alpha
beta'

check edit_file '1,$p\n1s/alpha/gamma/\nw\nq\n' '11
alpha
beta
11' out.txt
same_file edit_file out.txt 'gamma
beta'

check append_write 'a\nmore\n.\nW out.txt\nq\n' '5'
same_file append_write out.txt 'gamma
beta
more'

check read_into 'a\nfirst\n.\n0r out.txt\n1,$p\nQ\n' '16
gamma
beta
more
first'

check quit_guard 'a\nunsaved\n.\nq\n1p\nq\n' '?
unsaved'

check missing_file 'e nosuchfile\na\nstill works\n.\np\nQ\n' '?nosuchfile
still works'

long=$(printf 'n%.0s' $(seq 80))
check long_filename "a\nkept\n.\nw $long\n1p\nQ\n" '?
kept'

check quiet_mode 'a\nz\n.\nw q.txt\nq\n' '' -
same_file quiet_mode q.txt 'z'

check eof_ends 'a\nno final q\n.\n' '?'

# more lines than zero[] finds in one page: memory.grow, and the temp file
seq 1 15000 > big.txt
size=$(wc -c < big.txt)
check big_file '$p\n1,100s/^1/X/\n7500p\n1p\nw big2.txt\nq\n' "$size
15000
7500
X
$size" big.txt
total=$((total + 1))
if [ "$(sed -n 15000p big2.txt)" != 15000 ] || [ "$(sed -n 10p big2.txt)" != X0 ]; then
    failed=$((failed + 1))
    echo "FAIL big_file: big2.txt"
fi

# V7's limit: 255 blocks of 512 bytes of temp file. An error, not a crash.
check temp_file_limit '1,$s/^1/X/\n7500p\n1p\nQ\n' "$size
?TMP
7500
X" big.txt

echo "$total checks, $failed failed: $engine"
[ "$failed" -eq 0 ]
