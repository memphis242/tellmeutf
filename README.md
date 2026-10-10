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
| 1 | a name matched nothing |
| 2 | usage error (bad option, invalid UTF-8, code point out of range, ...) |
| 3 | a name was ambiguous |
| 4 | could not write the output |

Malformed arguments are rejected before any lookup runs. Lookups that merely
find nothing or are ambiguous do not stop the others; the status is that of the
first one that failed.

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
