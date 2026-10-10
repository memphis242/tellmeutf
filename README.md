# tellmeutf

A quick command-line lookup for Unicode characters. Give it a character, a code
point, or a name; it prints the UTF-32 value, the character and its name, with
the neighboring code points around it.

## Examples

A character:

```
$ tellmeutf ❯
  0x0000276D  U+276D  ❭   MEDIUM RIGHT-POINTING ANGLE BRACKET ORNAMENT
  0x0000276E  U+276E  ❮   HEAVY LEFT-POINTING ANGLE QUOTATION MARK ORNAMENT
> 0x0000276F  U+276F  ❯   HEAVY RIGHT-POINTING ANGLE QUOTATION MARK ORNAMENT
  0x00002770  U+2770  ❰   HEAVY LEFT-POINTING ANGLE BRACKET ORNAMENT
  0x00002771  U+2771  ❱   HEAVY RIGHT-POINTING ANGLE BRACKET ORNAMENT
```

A code point (`U+276F`, `0x276f`, `\u276f` and bare `276f` all work; quote the
backslash form in a shell, as in `'\u276f'`, because the shell would otherwise
strip the backslash):

```
$ tellmeutf -C 1 U+00E9
  0x000000E8  U+00E8  è   LATIN SMALL LETTER E WITH GRAVE
> 0x000000E9  U+00E9  é   LATIN SMALL LETTER E WITH ACUTE
  0x000000EA  U+00EA  ê   LATIN SMALL LETTER E WITH CIRCUMFLEX
```

A name (several words need no quotes; matching ignores case and hyphens):

```
$ tellmeutf -s heavy check mark
0x00002714  U+2714  ✔   HEAVY CHECK MARK
```

A name with several matches lists candidates and exits with status 3:

```
$ tellmeutf -m 2 arrow
tellmeutf: 'arrow' matches 646 characters; showing the first 2. Add more words to narrow it down, or raise --max to list more.
0x00002190  U+2190  ←   LEFTWARDS ARROW
0x00002191  U+2191  ↑   UPWARDS ARROW
```

A misspelled name still finds its character: when nothing matches strictly, the
closest names are tried (typos, words run together or split apart, plurals,
extra words). A clear winner is shown with a note on stderr:

```
$ tellmeutf -s snowmann
tellmeutf: no exact match for 'snowmann'; showing the closest: SNOWMAN (U+2603). Use --exact to turn off approximate matching.
0x00002603  U+2603  ☃   SNOWMAN
```

When no single match stands out, the closest matches are listed and the status
is 3:

```
$ tellmeutf -m 3 arrowz
tellmeutf: no exact match for 'arrowz'; these are the closest matches.
0x00002192  U+2192  →   RIGHTWARDS ARROW
0x00002190  U+2190  ←   LEFTWARDS ARROW
0x00002193  U+2193  ↓   DOWNWARDS ARROW
```

Several arguments give several blocks. Text that holds several code points (for
example a letter plus a combining accent) shows each one. Combining marks are
drawn on a dotted circle, and controls and spaces show a blank glyph, so the
row always stays readable.

Bare hex digits are always read as a number: `tellmeutf face` shows U+FACE. Use
`--name` to read it as a name instead.

## Options

```
-C N, --context=N   characters to show before and after (default 2)
-s, --single        show only the result (same as -C 0)
-n, --name          read the arguments as a name, even if they look like hex
-m N, --max=N       candidates to list for an ambiguous name (default 20)
-e, --exact         match names strictly; no approximate fallback for typos
-S, --semantic      find characters by the meaning of a description (optional, see below)
-h, --help          usage
-V, --version       print the version
--                  end of options
```

Options may come before or after the arguments. When context rows are shown
(context greater than 0) the target row is marked with `>` and, on a terminal,
bold; single-result output (`-s`) is plain. Nothing but plain UTF-8 text is
written to a pipe or file.

## Exit status

| status | meaning |
|--------|---------|
| 0 | every lookup succeeded |
| 1 | a name matched nothing, not even approximately |
| 2 | usage error (bad option, invalid UTF-8, code point out of range, ...) |
| 3 | a name was ambiguous, or only approximate matches were found and none stood out |
| 4 | could not write the output |
| 5 | semantic search is unavailable (not built in, model files missing, index cache problem) |

Malformed arguments are rejected before any lookup runs. Lookups that merely
find nothing or are ambiguous do not stop the others; the status is that of the
first one that failed.

## Optional: semantic search

`-S` (`--semantic`) finds characters by what a description means, instead of
matching the words of their names. It is meant for paraphrases that name
matching cannot reach: `degrees` finds DEGREE SIGN, `sad face` finds the
crying and disappointed faces, `music note` finds the musical notes. It uses a
small sentence-embedding model (all-MiniLM-L6-v2, 8-bit) that runs locally on
the CPU; nothing leaves the machine.

It replaces name matching for that run, so use the default mode for exact names.
It is weak where the answer needs outside knowledge (`tick` for CHECK MARK,
`love` for a heart) and for ASCII punctuation by its common name (`pipe`,
`backslash`); the default name matching does better there. In ranked lists
similar names crowd each other (BLACK and WHITE variants).

To enable it, download the model and ONNX Runtime once, then build with the
option on:

```
scripts/fetch-semantic.sh
cmake -S . -B build -DTELLMEUTF_SEMANTIC=ON
```

The script prints the exact `cmake` flags; add `-DONNXRUNTIME_ROOT=<dir>/onnxruntime`
when you passed it a directory other than the default. It verifies every
download against pinned SHA-256 checksums (about 35 MB in all). The files live in
`${XDG_DATA_HOME:-$HOME/.local/share}/tellmeutf/semantic` and the index is cached
in `${XDG_CACHE_HOME:-$HOME/.cache}/tellmeutf/semantic-index.bin`. The first use
builds that index (about 55 MB, around 8 seconds) and says so on stderr; it is
rebuilt automatically when the data changes. Set `TELLMEUTF_SEMANTIC_DIR` to use
another directory for the model files.

All arguments are joined into one description. The best matches are listed with
their cosine similarity (0 to 1, higher is closer) in front, up to `--max` rows;
matches below 0.30 are dropped. `-s` shows only the best match. `-C` has no
effect here, and `-e` cannot be combined with `-S`.

```
$ tellmeutf -S -m 5 sad face
0.66  0x0001F622  U+1F622  😢  CRYING FACE
0.64  0x0001F61E  U+1F61E  😞  DISAPPOINTED FACE
0.60  0x0001F620  U+1F620  😠  ANGRY FACE
0.60  0x0001F979  U+1F979  🥹  FACE HOLDING BACK TEARS
0.58  0x0001F972  U+1F972  🥲  SMILING FACE WITH TEAR

$ tellmeutf -S -s degrees
0.68  0x000000B0  U+00B0  °   DEGREE SIGN
```

The scores are not probabilities: judge a result by the gap between rows. A
description that is close to nothing prints a message and exits with status 1;
a build without the feature, missing model files or an unusable cache exit with
status 5 and say what to do.

## Building

Requires CMake 3.25 or newer and a C++23 compiler.

```
cmake -S . -B build -G Ninja
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

The binary is `build/src/tellmeutf`.

## Unicode data

The Unicode Character Database files are vendored under `data/ucd/` and
compiled into the program at build time, so there is no runtime file access.
See `docs/design.md` for the details.
