# tellmeutf design

`tellmeutf` is a quick command-line lookup for Unicode characters. Give it a
character, a code point, or a name; it prints the character's UTF-32 value, the
character, and its name, together with the neighboring code points.

The default invocation takes no flags: `tellmeutf <thing>`. The program works
out what kind of thing it was given. Options exist only for the rarer cases.

## Behavior

### Argument interpretation

Interpretation is purely syntactic (it never consults the Unicode data), so it
is a pure function of the argument strings and the `--name` flag. Each argument
is classified in this order:

1. **Text**: contains any non-ASCII byte. Decoded as strict UTF-8 (overlong
   forms, surrogates, values above U+10FFFF and truncated sequences are
   rejected with a usage error). Every code point in it becomes its own lookup,
   so pasting `é` written as `e` + U+0301, or an emoji with a variation
   selector, shows each part.
2. **Character**: exactly one ASCII byte (`a`, `1`, `-`). That character.
3. **Prefixed code point**: `U+`, `u+`, `0x`, `0X`, `\u`, `\U` followed by 1 to
   8 hex digits (`U+276F`, `0x276f`). A recognized prefix followed by anything
   that is not 1 to 8 hex digits is a usage error, never a name.
4. **Bare code point**: two or more characters, all hex digits (`276f`, `a9`,
   `FF`). A string of hex digits is always a number, even when it is also an
   English word or a character name (`face`, `bed`) or a control abbreviation
   (`FF`, `DC1`); `--name` reads it as a name instead.
5. **Word**: anything else (`snowman`, `right-pointing`, `"check mark"`).

Then:

- If any argument is a Word, or `--name` is given, all arguments are joined with
  single spaces into one **name** lookup. (So `tellmeutf heavy check mark` works
  without quotes.)
- Otherwise every argument (and, for Text, every code point of it) is its own
  **code point** lookup, processed in order.

When the arguments are joined into a name, Text arguments must still be valid
UTF-8, and an argument with a code point prefix must still be well formed (so
`U+zz snowman` is a usage error); `--name` reads such arguments as plain name
text.

A code point above U+10FFFF is a usage error. Surrogates (U+D800 to U+DFFF) are
accepted as code points and shown with the label `<surrogate>`.

### Name matching

Names are matched against the official Unicode names plus every alias in
`NameAliases.txt` (control names, abbreviations such as `NBSP`, `ZWJ`, `LF`,
corrections, and so on), plus the algorithmic names (Hangul syllables and the
ideograph ranges, see below).

Words. A name or query is uppercased and split into words at whitespace, at
underscores, and at a hyphen that sits between two letters/digits (a medial
hyphen). A hyphen anywhere else stays part of its word, which keeps
`TIBETAN LETTER -A` (U+0F60) distinct from `TIBETAN LETTER A` (U+0F68).
`right-pointing`, `RIGHT POINTING` and `right_pointing` are the same words.

- **Exact**: the query's word sequence equals the word sequence of a name or
  alias. An exact match wins over everything else. If more than one code point
  matches exactly, the result is ambiguous.
- **Search** (only when nothing matches exactly): a code point matches if every
  query word is a prefix of some word in one of its names or aliases. Results
  are ranked best first: (1) every query word equals a whole name word, before
  prefix-only matches; (2) fewer words in the matching label first; (3) lower
  code point first. A single result is treated like an exact match; several results are
  ambiguous.

### Output

One row per code point:

```
  0x0000276D  U+276D  ❭   MEDIUM RIGHT-POINTING ANGLE BRACKET ORNAMENT
  0x0000276E  U+276E  ❮   HEAVY LEFT-POINTING ANGLE QUOTATION MARK ORNAMENT
> 0x0000276F  U+276F  ❯   HEAVY RIGHT-POINTING ANGLE QUOTATION MARK ORNAMENT
  0x00002770  U+2770  ❰   HEAVY LEFT-POINTING ANGLE BRACKET ORNAMENT
  0x00002771  U+2771  ❱   HEAVY RIGHT-POINTING ANGLE BRACKET ORNAMENT
```

Columns, separated by two spaces:

1. Marker (`>` on the target row, blank otherwise). Present only when context
   rows are requested (context greater than 0); omitted for single-result mode.
2. The UTF-32 code unit, `0x` plus 8 uppercase hex digits.
3. `U+` plus at least 4 uppercase hex digits, padded on the right to the widest
   such cell in the block.
4. The glyph cell, padded on the right to 2 terminal cells (see Glyph cell).
5. The name.

When context rows are shown (context greater than 0), the target row is bold if
stdout is a terminal (and `TERM` is not `dumb`). Single-result output (`-s`,
`-C 0`) has no marker and no bold. No escape sequences are ever written to a
pipe or file. All output is UTF-8
regardless of locale.

The context window is the numerically adjacent code points, clipped at U+0000
and U+10FFFF (no wrap-around). Unassigned neighbors are shown with a label name
rather than skipped. Several lookups print as blocks separated by one blank line.

Names that do not exist are labelled: `<unassigned>`, `<noncharacter>`,
`<private use>`, `<surrogate>`, `<control>` (a control with no alias).
Control characters show their alias name (`LINE FEED`, `NULL`, ...).

#### Glyph cell

Printing a raw character is unsafe for some categories (controls move the
cursor, bidirectional controls reorder the row, combining marks attach to the
neighbor). The glyph cell holds:

- Lu Ll Lt Lm Lo Nd Nl No Pc Pd Ps Pe Pi Pf Po Sm Sc Sk So Co: the character.
- Mn Mc Me: U+25CC DOTTED CIRCLE followed by the character (one cell wide).
- Zs Zl Zp Cc Cf Cs Cn: a single space (the name says what it is).

Width in cells is 2 when the code point is East Asian Wide or Fullwidth
(`EastAsianWidth.txt`, W or F), otherwise 1.

### Ambiguous and failed names

- Ambiguous: print up to `--max` (default 20) candidate rows to stdout (same row
  format, no marker, no context), then a note to stderr saying how many matched
  and how to narrow. Exit status 3. Single-result mode behaves identically.
- Not found: a message to stderr, exit status 1.

### Options

```
-C N, --context=N   characters to show before and after (default 2)
-s, --single        show only the result (same as -C 0)
-n, --name          read the arguments as a name, even if they look like hex
-m N, --max=N       candidates to list for an ambiguous name (default 20)
-h, --help          usage
-V, --version       "tellmeutf 0.1.0 (Unicode 18.0.0)"
--                  end of options
```

Options may appear before or after the arguments. `-CN`, `-C N`, `--context=N`
and `--context N` are all accepted. A lone `-` is an argument, not an option.
`N` must be a non-negative decimal integer no larger than 1114111 (`-C`) or
100000 (`-m`, minimum 1).

With no arguments the usage goes to stderr and the status is 2.

### Exit status

| status | meaning |
|--------|---------|
| 0 | every lookup succeeded |
| 1 | a name matched nothing |
| 2 | usage error (bad option, invalid UTF-8, code point out of range, ...) |
| 3 | a name was ambiguous |
| 4 | could not write the output |

Malformed arguments (invalid UTF-8, a bad code point, a bad option) are rejected
before any lookup runs, so nothing is printed for the valid arguments beside
them. Lookups that merely find nothing or are ambiguous do not stop the others:
all are attempted and the status is that of the first one that failed. A write
failure on stdout stops the run with status 4. SIGPIPE keeps its default
action, so a reader that closes the pipe early (`| head -1`) ends the process
quietly.

Error messages go to stderr, start with `tellmeutf: `, say what went wrong and
what to do about it, and never apologize. Text from the command line that a
message echoes is escaped: control bytes, bytes that are not valid UTF-8 and
bidirectional formatting characters appear as `\xNN` or `\u{XXXX}`.

## Architecture

```
data/ucd/*.txt          vendored Unicode Character Database files (pinned version)
tools/gen_ucd.cpp       build-time generator: UCD text -> ucd_tables.cpp
src/ucd.hpp             PUBLIC CONTRACT of the lookup library (names, categories, widths)
src/ucd_tables.hpp      internal: shape of the generated tables
src/ucd.cpp             lookup + name search over the generated tables
src/utf8.{hpp,cpp}      strict UTF-8 decode / encode
src/query.{hpp,cpp}     argument interpretation (pure; no UCD access)
src/options.{hpp,cpp}   command-line option parsing (pure; no globals)
src/render.{hpp,cpp}    glyph cell and row rendering
src/main.cpp            wiring, I/O, exit status
tests/                  unit tests (tests/check.hpp), tests/e2e.sh
```

Targets: `tellmeutf_ucd` (static library: ucd.cpp + generated tables),
`tellmeutf_core` (static library: utf8, query, options, render; links
`tellmeutf_ucd`), `tellmeutf` (executable).

### Data pipeline

`gen_ucd` runs at build time (CMake `add_custom_command`) over the vendored
files and emits one C++ source file with the tables, so there is no runtime
file access and no download at build time. Updating Unicode means running
`scripts/update-ucd.sh <version>`, rebuilding, and fixing whatever the
generator rejects.

The generator reports malformed input with file, line number and a plain
message, exits non-zero, and refuses unknown `First`/`Last` range names rather
than guessing (a new Unicode version adding a range must be taught explicitly).

Tables carry: for every explicitly listed code point its general category and
name; the `First`/`Last` ranges with their derivation rule; all aliases with
their type; the East Asian Wide/Fullwidth ranges; the Unicode version (parsed
from the header of `NameAliases.txt`). Categories are emitted as
`tellmeutf::ucd::Category::Xx` identifiers, never as bare numbers, so the
enum order in `ucd.hpp` is free to change.

Algorithmic names, derived in `ucd.cpp`:

- `HANGUL SYLLABLE <L><V><T>` for U+AC00 to U+D7A3 (Unicode core spec 3.12,
  short jamo names from `Jamo.txt`, frozen).
- `CJK UNIFIED IDEOGRAPH-<hex>` for the `CJK Ideograph*` ranges,
  `TANGUT IDEOGRAPH-<hex>` for `Tangut Ideograph*`, `JURCHEN CHARACTER-<hex>`,
  `SEAL CHARACTER-<hex>`; hex is uppercase, at least 4 digits. These ranges are
  matched by exact name only (parse the suffix); they are never enumerated for
  search. Hangul syllables are searchable.
- Surrogate and private-use ranges have no name (labels above).

Display name of a control (`Cc`): the first `control` alias, else the first
`figment` alias, else `<control>`.

## Conventions

C++23 (`std::expected` for fallible calls, no exceptions for control flow), no
third-party dependencies. Const-correct and read-only by default; internals
private (anonymous namespace / `static`). `struct` qualifier on declarations of
struct types (`const struct CharInfo info = ...`). `assert` for conditions the
program controls (never for user input). Prefer `switch` and maps to if-chains.
Comments describe the code as it is now. Plain ASCII everywhere: no em dashes,
no emojis. Builds clean with the warning set in the top-level CMakeLists.txt.

## Testing

`tests/check.hpp` provides `CHECK(cond)` (active in every build type).
Unit tests cover realistic inputs and corner cases at function level; they do
not pin whitespace or layout. `tests/e2e.sh <path-to-tellmeutf>` drives the real
binary and checks exit status and the presence of key facts (code point,
name), not column arrangement. Build trees live under `/workspace/build/`.
