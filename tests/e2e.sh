#!/usr/bin/env bash
# End-to-end test: drives the real binary. Usage: bash tests/e2e.sh /path/to/tellmeutf
# Checks exit status and the presence of key facts, not column arrangement.
set -u

if [[ $# -ne 1 || ! -x $1 ]]; then
    echo "usage: e2e.sh /path/to/tellmeutf" >&2
    exit 2
fi
bin=$1

scratch=$(mktemp -d)
trap 'rm -rf "$scratch"' EXIT
out=$scratch/out
err=$scratch/err

failures=0
fail() {
    echo "FAIL: $*" >&2
    failures=$((failures + 1))
}

# run <args...>: runs the binary with stdout and stderr captured; sets $status.
run() {
    "$bin" "$@" >"$out" 2>"$err" </dev/null
    status=$?
}

expect_status() {
    local want=$1
    shift
    run "$@"
    [[ $status -eq $want ]] || fail "'$*': status $status, wanted $want"
}

out_has() { grep -qF -- "$1" "$out" || fail "'${cmd:-}': stdout lacks '$1'"; }
out_lacks() { ! grep -qF -- "$1" "$out" || fail "'${cmd:-}': stdout has '$1'"; }
err_has_text() { [[ -s $err ]] || fail "'${cmd:-}': stderr is empty"; }
rows() { grep -c . "$out"; }

heart=$'\xe2\x9d\xaf'  # U+276F
name_276f='HEAVY RIGHT-POINTING ANGLE QUOTATION MARK ORNAMENT'

# The three input kinds and their spellings all reach U+276F.
for arg in "$heart" 276f U+276F 0x276f u+276f '\u276f' "heavy right-pointing angle quotation mark ornament"; do
    cmd=$arg
    run "$arg"
    [[ $status -eq 0 ]] || fail "'$arg': status $status"
    out_has 0x0000276F
    out_has U+276F
    out_has "$name_276f"
done
cmd=

# Name given as several words without quotes.
cmd="heavy right-pointing angle quotation mark ornament"
run heavy right-pointing angle quotation mark ornament
[[ $status -eq 0 ]] || fail "$cmd: status $status"
out_has "$name_276f"

# Default context is 2 on each side with a marker on the target.
cmd=default
run U+276F
[[ $(rows) -eq 5 ]] || fail "default context: expected 5 rows, got $(rows)"
out_has 0x0000276D
out_has 0x00002771
[[ $(grep -c '^>' "$out") -eq 1 ]] || fail "expected exactly one marked row"

# -s prints exactly one row without a marker; so does -C 0.
for opts in "-s" "--single" "-C 0"; do
    cmd="$opts U+276F"
    # shellcheck disable=SC2086
    run $opts U+276F
    [[ $status -eq 0 ]] || fail "$cmd: status $status"
    [[ $(rows) -eq 1 ]] || fail "$cmd: expected 1 row, got $(rows)"
    out_lacks '>'
    out_has "$name_276f"
done

# Context in every spelling, before or after the argument.
for opts in "-C 4 U+276F" "-C4 U+276F" "--context=4 U+276F" "--context 4 U+276F" "U+276F -C 4" "U+276F -C4" "U+276F --context=4"; do
    cmd=$opts
    # shellcheck disable=SC2086
    run $opts
    [[ $status -eq 0 ]] || fail "$cmd: status $status"
    [[ $(rows) -eq 9 ]] || fail "$cmd: expected 9 rows, got $(rows)"
done

# Windows clip at both ends of the code space.
cmd="U+0"
run U+0 -C 2
[[ $status -eq 0 ]] || fail "$cmd: status $status"
[[ $(rows) -eq 3 ]] || fail "start of range: expected 3 rows, got $(rows)"
out_has 0x00000000
out_lacks 0xFFFFFFFF
cmd="U+10FFFF"
run U+10FFFF -C 2
[[ $status -eq 0 ]] || fail "$cmd: status $status"
[[ $(rows) -eq 3 ]] || fail "end of range: expected 3 rows, got $(rows)"
out_has 0x0010FFFF
out_lacks 0x00110000
cmd=

# Surrogates are accepted.
expect_status 0 -s U+D800

# Two arguments produce two blocks separated by one blank line.
cmd="U+41 U+5A"
run U+41 U+5A -C 1
[[ $status -eq 0 ]] || fail "$cmd: status $status"
out_has 0x00000041
out_has 0x0000005A
[[ $(rows) -eq 6 ]] || fail "two blocks: expected 6 rows, got $(rows)"
[[ $(grep -c '^$' "$out") -eq 1 ]] || fail "two blocks: expected one blank separator line"

# Non-ASCII text yields one lookup per code point.
cmd="e + combining acute"
run -s $'e\xcc\x81'
[[ $status -eq 0 ]] || fail "$cmd: status $status"
[[ $(rows) -eq 2 ]] || fail "$cmd: expected 2 rows, got $(rows)"
out_has 0x00000065
out_has 0x00000301

# --name reads hex-looking input as a name; plain 'face' is a number.
cmd="face"
run -s face
[[ $status -eq 0 ]] || fail "$cmd: status $status"
out_has 0x0000FACE
cmd="--name face"
run --name face
[[ $status -eq 0 || $status -eq 3 ]] || fail "$cmd: status $status"
out_lacks 0x0000FACE
out_has FACE
cmd="-n face (short)"
run -n face
out_lacks 0x0000FACE

# Usage errors exit with status 2 and say something on stderr.
for args in "" "U+110000" "110000" "U+" "0xzz" "--bogus x" "-C" "-C x a" "-C 1114112 a" "-m 0 a" "-m 100001 a"; do
    cmd="[$args]"
    # shellcheck disable=SC2086
    run $args
    [[ $status -eq 2 ]] || fail "$cmd: status $status, wanted 2"
    err_has_text
    [[ ! -s $out ]] || fail "$cmd: stdout should be empty"
done
cmd="invalid UTF-8"
run $'\xff'
[[ $status -eq 2 ]] || fail "$cmd: status $status, wanted 2"
run $'\xed\xa0\x80'
[[ $status -eq 2 ]] || fail "surrogate bytes: status $status, wanted 2"
run $'\xe2\x9d'
[[ $status -eq 2 ]] || fail "truncated sequence: status $status, wanted 2"
run $'\xc0\x80'
[[ $status -eq 2 ]] || fail "overlong form: status $status, wanted 2"
# Malformed arguments are rejected before any lookup runs, so valid ones beside them print nothing.
cmd="U+41 U+110000"
run U+41 U+110000
[[ $status -eq 2 ]] || fail "$cmd: status $status, wanted 2"
[[ ! -s $out ]] || fail "$cmd: stdout should be empty"

# A name that matches nothing: status 1, message on stderr.
cmd="nonsense name"
run qzxjv nonsensename
[[ $status -eq 1 ]] || fail "$cmd: status $status, wanted 1"
err_has_text
[[ ! -s $out ]] || fail "$cmd: stdout should be empty"

# Approximate fallback: a typo resolves to the closest name with a note on stderr.
cmd="snowmann"
run -s snowmann
[[ $status -eq 0 ]] || fail "$cmd: status $status, wanted 0"
[[ $(rows) -eq 1 ]] || fail "$cmd: expected 1 row, got $(rows)"
out_has U+2603
out_has SNOWMAN
err_has_text
grep -qF -- "snowmann" "$err" || fail "$cmd: note should echo the query"
grep -qF -- "U+2603" "$err" || fail "$cmd: note should name the code point"
cmd="chek mark"
run -s chek mark
[[ $status -eq 0 || $status -eq 3 ]] || fail "$cmd: status $status"
out_has U+2713
err_has_text
run snowmann
[[ $status -eq 0 && $(rows) -eq 5 ]] || fail "snowmann with context: status $status rows $(rows)"
[[ $(grep -c '^>' "$out") -eq 1 ]] || fail "snowmann with context: expected one marked row"

# --exact (and -e) turn the fallback off.
for flag in --exact -e; do
    cmd="$flag snowmann"
    run "$flag" snowmann
    [[ $status -eq 1 ]] || fail "$cmd: status $status, wanted 1"
    err_has_text
    [[ ! -s $out ]] || fail "$cmd: stdout should be empty"
done
run -e snowman
[[ $status -eq 0 ]] || fail "-e snowman (strict match): status $status"
out_has U+2603

# A vague misspelling lists the closest matches with status 3.
cmd="arrowz"
run arrowz
[[ $status -eq 3 ]] || fail "$cmd: status $status, wanted 3"
err_has_text
[[ $(rows) -gt 1 ]] || fail "$cmd: expected several candidate rows"
out_lacks '>'
out_has ARROW
run -m 3 arrowz
[[ $status -eq 3 && $(rows) -eq 3 ]] || fail "-m 3 arrowz: status $status rows $(rows)"
run -e arrowz
[[ $status -eq 1 ]] || fail "-e arrowz: status $status, wanted 1"
cmd=

# Strict matches are untouched by the fallback.
run -s heavy check mark
[[ $status -eq 0 && $(rows) -eq 1 ]] || fail "strict name: status $status rows $(rows)"
out_has U+2714
[[ ! -s $err ]] || fail "strict name: stderr should be empty"

# CLDR short names and keywords find characters by their everyday names, with
# or without --exact (they are not approximate matches).
for flag in "" -e; do
    cmd="$flag backslash"
    run $flag -s backslash
    [[ $status -eq 0 && $(rows) -eq 1 ]] || fail "$cmd: status $status rows $(rows)"
    out_has U+005C
    [[ ! -s $err ]] || fail "$cmd: stderr should be empty"
done
cmd="backslash with context"
run backslash
[[ $status -eq 0 && $(grep -c '^>' "$out") -eq 1 ]] || fail "$cmd: status $status"
out_has U+005C
cmd="woman's boot"
run -s "woman's boot"
[[ $status -eq 0 ]] || fail "$cmd: status $status"
out_has U+1F462
cmd="tick"
run -e -m 30 tick
[[ $status -eq 3 ]] || fail "$cmd: status $status, wanted 3"
out_has U+2714
err_has_text
# The CLDR keyword ranks ahead of the longer official names that contain TICK.
heavy_line=$(grep -n 'U+2714' "$out" | head -1 | cut -d: -f1)
square_line=$(grep -n 'U+27E4' "$out" | head -1 | cut -d: -f1)
[[ -n $heavy_line && -n $square_line && $heavy_line -lt $square_line ]] || fail "$cmd: U+2714 should come before U+27E4"
cmd=

# Echoed user text is escaped: an ESC byte in a name never reaches stderr.
cmd="ESC in name"
run $'qzxjv\x1b[2Jnope'
[[ $status -eq 1 ]] || fail "$cmd: status $status, wanted 1"
if [[ $(LC_ALL=C tr -cd '\033' <"$err" | wc -c) -ne 0 ]]; then
    fail "$cmd: escape byte found on stderr"
fi
run -$'\x1b'
[[ $status -eq 2 ]] || fail "ESC option: status $status, wanted 2"
if [[ $(LC_ALL=C tr -cd '\033' <"$err" | wc -c) -ne 0 ]]; then
    fail "ESC option: escape byte found on stderr"
fi

# An ambiguous word: candidate rows on stdout, a note on stderr, status 3.
cmd="arrow"
run arrow
[[ $status -eq 3 ]] || fail "$cmd: status $status, wanted 3"
err_has_text
[[ $(rows) -gt 1 ]] || fail "$cmd: expected several candidate rows"
[[ $(rows) -eq 20 ]] || fail "$cmd: expected 20 candidates by default, got $(rows)"
out_has ARROW
out_lacks '>'
run -m 3 arrow
[[ $status -eq 3 ]] || fail "-m 3 arrow: status $status, wanted 3"
[[ $(rows) -eq 3 ]] || fail "-m 3 arrow: expected 3 rows, got $(rows)"
run arrow -s
[[ $status -eq 3 && $(rows) -eq 20 ]] || fail "arrow -s: status $status rows $(rows)"


# Help and version.
cmd="--help"
run --help
[[ $status -eq 0 ]] || fail "$cmd: status $status"
[[ -s $out ]] || fail "$cmd: help text should be on stdout"
[[ ! -s $err ]] || fail "$cmd: stderr should be empty"
out_has tellmeutf
run -h
[[ $status -eq 0 && -s $out ]] || fail "-h: status $status"
for flag in -V --version; do
    cmd=$flag
    run "$flag"
    [[ $status -eq 0 ]] || fail "$cmd: status $status"
    out_has tellmeutf
    out_has Unicode
    [[ $(rows) -eq 1 ]] || fail "$cmd: expected one line"
done

# A lone dash is a character; -- ends options.
cmd="-"
run -s -
[[ $status -eq 0 ]] || fail "$cmd: status $status"
out_has 0x0000002D
run -s -- -
[[ $status -eq 0 ]] || fail "-- -: status $status"
out_has 0x0000002D

# Output through a pipe or file never contains an escape byte.
cmd="pipe"
"$bin" U+276F 2>/dev/null | cat >"$out"
if [[ $(LC_ALL=C tr -cd '\033' <"$out" | wc -c) -ne 0 ]]; then
    fail "escape byte found in piped output"
fi
[[ -s $out ]] || fail "piped output is empty"
TERM=xterm "$bin" U+276F >"$out" 2>/dev/null
if [[ $(LC_ALL=C tr -cd '\033' <"$out" | wc -c) -ne 0 ]]; then
    fail "escape byte found in redirected output"
fi

# A write failure is reported with status 4 (a full device).
if [[ -w /dev/full ]]; then
    "$bin" U+276F >/dev/full 2>"$err"
    status=$?
    [[ $status -eq 4 ]] || fail "/dev/full: status $status, wanted 4"
    [[ -s $err ]] || fail "/dev/full: stderr is empty"
fi

# Semantic search (-S). A build without it exits 5 and prints nothing on stdout;
# a build with it is exercised only when the model files are installed.
# A private cache keeps the user's real index untouched.
export XDG_CACHE_HOME=$scratch/cache
cmd="-S usage"
expect_status 2 -S -e x
expect_status 2 -S --exact x
expect_status 2 -S

run -S x
if [[ $status -eq 5 && $(grep -c . "$err") -ge 1 ]] && grep -q TELLMEUTF_SEMANTIC "$err"; then
    [[ ! -s $out ]] || fail "-S in a build without semantic search: stdout should be empty"
else
    semantic_dir=${TELLMEUTF_SEMANTIC_DIR:-${XDG_DATA_HOME:-$HOME/.local/share}/tellmeutf/semantic}
    if [[ -f $semantic_dir/model.onnx && -f $semantic_dir/vocab.txt ]]; then
        export TELLMEUTF_SEMANTIC_DIR=$semantic_dir

        cmd="-S poop"
        run -S poop
        [[ $status -eq 0 ]] || fail "$cmd: status $status"
        out_has U+1F4A9
        [[ $(rows) -le 20 ]] || fail "$cmd: more rows than --max"

        cmd="-S -s trash can"
        run -S -s trash can
        [[ $status -eq 0 ]] || fail "$cmd: status $status"
        [[ $(rows) -eq 1 ]] || fail "$cmd: expected 1 row, got $(rows)"

        # CLDR keywords give the model rows for everyday words that no name
        # contains: tick, pipe.
        cmd="-S -m 10 tick"
        run -S -m 10 tick
        [[ $status -eq 0 ]] || fail "$cmd: status $status"
        out_has U+2714
        cmd="-S -m 10 pipe"
        run -S -m 10 pipe
        [[ $status -eq 0 ]] || fail "$cmd: status $status"
        out_has U+007C

        cmd="-S -m 3 sad face"
        run -S -m 3 sad face
        [[ $status -eq 0 && $(rows) -le 3 && $(rows) -ge 1 ]] || fail "$cmd: status $status rows $(rows)"

        cmd="-S nonsense"
        run -S qwertyuiop zxcvbnm
        [[ $status -eq 0 || $status -eq 1 ]] || fail "$cmd: status $status"
        if [[ $status -eq 1 ]]; then
            [[ ! -s $out ]] || fail "$cmd: stdout should be empty on status 1"
        fi

        # A missing model is status 5 with a message on stderr only.
        cmd="-S without model files"
        TELLMEUTF_SEMANTIC_DIR=$scratch/no-such-dir "$bin" -S poop >"$out" 2>"$err" </dev/null
        status=$?
        [[ $status -eq 5 && ! -s $out && -s $err ]] || fail "$cmd: status $status"
    else
        echo "e2e: semantic search checks skipped (no model files in $semantic_dir)"
    fi
fi
cmd=

if [[ $failures -ne 0 ]]; then
    echo "$failures check(s) failed" >&2
    exit 1
fi
echo "e2e: all checks passed"
