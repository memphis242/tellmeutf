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
ideograph ranges, see below), plus the CLDR short names and keywords (see
"CLDR annotations" below). Together these are the *labels* of a code point.

Words. A name or query is uppercased, stripped of apostrophes (`'` and the
typographic U+2019: `woman's` is `womans`; no official name has one), and split
into words at whitespace, at underscores, and at a hyphen that sits between two
letters/digits (a medial hyphen). A hyphen anywhere else stays part of its word,
which keeps `TIBETAN LETTER -A` (U+0F60) distinct from `TIBETAN LETTER A`
(U+0F68). `right-pointing`, `RIGHT POINTING` and `right_pointing` are the same
words.

- **Exact**: the query's word sequence equals the word sequence of a name or
  alias. An exact match wins over everything else. If more than one code point
  matches exactly, the result is ambiguous. When no name or alias matches
  exactly, a CLDR short name that does (`backslash` for U+005C) counts as the
  exact match.
- **Search** (only when nothing matches exactly): a code point matches if every
  query word is a prefix of some word in one of its labels (names, aliases,
  CLDR short name, CLDR keywords; one label at a time, so the words must all
  come from the same label). Results
  are ranked best first: (1) every query word equals a whole label word, before
  prefix-only matches; (2) fewer words in the matching label first; (3) lower
  code point first. A single result is treated like an exact match; several results are
  ambiguous.
- **Approximate** (only when nothing matches strictly, and not with `--exact`):
  see the next subsection.

#### CLDR annotations

`data/cldr/annotations/en.xml` (CLDR 48.2, English) gives many characters a
short name and a handful of keywords written for people searching by
everyday words: U+002F has the short name `slash` and the keywords `oblique`,
`solidus`, `stroke`, `virgule`, `whack`; U+2714 has the keyword `tick`; U+005C
has the short name `backslash` (its Unicode name is REVERSE SOLIDUS); U+2764 has
`love`. Only the annotations of single code points are used (1,768 characters
with 1,751 short names and 9,101 keywords after normalization; emoji sequences
are ignored). The generator turns each text into the form of a name (upper
case ASCII words: apostrophes removed, other punctuation read as a space, Latin
letters with diacritics folded to their base letter) and drops a text it cannot
reduce to that (arithmetic symbols, ranges such as `1-2`).

How they take part:

- A short name is a name only when no official name or alias matches: it is
  CLDR's own canonical name for the character, and each short name belongs to
  one character. Official names always win, so `check mark` stays U+2713 even
  though CLDR calls U+2714 `check mark`.
- A keyword is not a name (it is shared: 1,344 of 3,489 distinct keywords belong
  to more than one character), so it never makes an exact match. It is a label like any
  other in search, approximate matching and semantic search, so `tick` lists the
  characters called `tick` before the longer official names that contain TICK.
- In approximate matching a CLDR label earns 95% of the score its evidence
  would give an official label (`kAnnotationFactor`), just over the confidence
  margin, so when an official name and a keyword match equally well the official
  name wins and stays confident: `snowmann` is SNOWMAN even though CLDR also
  calls U+26C4 `snowman`.
- Semantic search embeds the texts as additional rows, discounted at ranking
  time (see "Semantic search").

The files are paired with Unicode 18.0.0; characters added in Unicode 18 have no
annotation until a newer CLDR release is vendored.

#### Approximate fallback

A name that has no exact and no prefix match is scored against every label
(names, aliases, CLDR short names and keywords; Hangul syllable names only when
the query has a word similar to HANGUL or SYLLABLE) by information-retrieval style similarity,
so typos and loose phrasing still find their character. The ideograph ranges
are not enumerated here either. The query must be ASCII and at most 200 bytes
(otherwise there is no approximate match), and only its first 12 words count.
Two signals are fused into one score in (0, 1]:

- **Word signal.** A query word is similar to a label word when they are equal,
  share a light stem (trailing -ING, -ED, -LY, -ES, -S and E removed: `smile`
  and `smiling`, `arrows` and `arrow`), the query word is a prefix of it, or
  they are within a few edits (adjacent transpositions count as one edit; none
  allowed for 1 to 2 letters, one for 3 to 5, two from 6; never for words with
  digits). Edit matches are weighted by how common the label word is, so `hart`
  resolves to HEART before HARP. Words are weighted by idf over the label
  vocabulary, so a rare word (SNOWMAN) outweighs a common one (SIGN, LETTER).
  A label's word score is the F-measure of recall (how much of the query's
  weight the label covers) and precision (how much of the label's weight the
  query explains), recall weighted higher, so `snowman symbol` still finds
  SNOWMAN and a long label does not beat a tight one. Word order is ignored.
- **Character signal.** Dice similarity of the character trigrams of the query
  and the label with spaces and punctuation removed, which survives words run
  together or split apart (`checkmark`, `pile ofpoo`).

The signals combine as independent evidence, so agreement raises a label above
what either gives alone. A CLDR label's score is then multiplied by 0.95 (see
"CLDR annotations"). Each code point keeps its best label; results are
ordered by score, then fewer words, then lower code point.

The result is *confident* (the closest match may be shown as the answer) only
when all of these hold:

- its score reaches a minimum and leads the second by a margin (or it is the
  only result); and
- one signal carries it by itself. Either the word score reaches a minimum and
  every matched query word is a strong match (equal, same stem, a proper prefix
  of at least 4 letters covering most of the label word, one edit on a word of
  6 or more letters, two edits on a word of 8 or more letters that occurs in
  some Unicode name or alias: a word found only in CLDR text is an everyday
  English word with many neighbors two edits away, so `backtick` is not
  `backpack`); a weak match on
  a shorter word is accepted when another query word matches strongly or when
  every word of a multi-word query matches. Or the query is essentially the
  label with its spaces removed: the trigram Dice similarity and the share of
  the query's trigrams found in the label are both high, for a query of at
  least six trigrams.

So `snowmann`, `strawbery` and `fullstop` are confident, while a single short
word that is two edits from some other word (`annual` for MANUAL) or that only
hides inside a longer name (`allow` in SHALLOW PAN OF FOOD) is listed instead.
A word that several characters list equally well (`checkmark`, a CLDR keyword
of U+2705 and U+2714) is a list too. Behavior:

- Nothing similar enough: the not-found error, status 1.
- Confident: shown like a single result (the context window per `-C`), status
  0, with a note on stderr: `no exact match for '<name>'; showing the closest:
  <NAME> (U+XXXX). Use --exact to turn off approximate matching.`
- Otherwise (including a best match without such evidence): up to `--max` candidate rows (same format as the ambiguous list)
  and a note on stderr, status 3.

The index (vocabulary, idf, inverted lists) is built on the first approximate
lookup, so strict lookups never pay for it (about 20 ms once, then about 1 ms
per query in a release build). Single-result mode (`-s`) behaves the same; the
note still goes to stderr.

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
- Not found: a message to stderr, exit status 1. This is after the approximate
  fallback finds nothing similar (always, with `--exact`).
- Approximate list: when only approximate matches exist and none stands out,
  exit status 3 (see "Approximate fallback").

### Semantic search

An optional, opt-in mode: `-S, --semantic` finds characters by the meaning of a
free-text description, for paraphrases that name matching cannot reach. It
replaces the lexical path for that run (no exact, word-prefix or approximate
matching), implies `--name` (all arguments are joined into one description, hex-looking words
are not numbers), and cannot be combined with `-e/--exact` (usage error, status 2).

Design:

- Model: all-MiniLM-L6-v2, the 8-bit dynamically quantized ONNX export
  (`model_quint8_avx2.onnx`, 23 MB), run with ONNX Runtime on the CPU. Tokens
  come from a BERT-uncased WordPiece tokenizer (`src/wordpiece.cpp`, vocabulary
  vendored in `data/semantic/`); a sentence vector is the mean of the token
  vectors over the attention mask, scaled to unit length. Queries get no prefix.
- Corpus: `ucd::descriptive_labels()`, about 45.5 thousand labels: names and
  aliases (not the algorithmic names, nor names that only spell their own code
  point), then the CLDR short names and keywords that are not already a name of
  the same code point (about 35.4 thousand names, 0.5 thousand aliases, 0.6
  thousand short names, 9.1 thousand keywords). Each label is one row of the index.
- Row weights: ranking multiplies a row's similarity by a weight set from its
  label kind, in `src/semantic.cpp`: names and aliases 1.0, CLDR short names
  0.99, CLDR keywords 0.7. A one-word query is equal to the keyword row of
  every character that lists the word, so unweighted those rows (all at about
  0.99) tie with the right answer and crowd it out of the top rows (`star`
  listed the asterisk and other symbols that merely list the word before U+2B50), and which of
  the ties comes first is decided by rounding noise. With the weights the
  official and short names lead and a keyword leads only when nothing else is
  close, which is how `tick`, `pipe` and `love` get answered. The reported score
  is the weighted one, so a keyword-only hit shows at most about 0.69 and the
  list stays in descending order. The weights are applied at query time, so
  changing them does not invalidate the cache.
- Search: brute force. The index is a flat float32 matrix; the query is scored
  against every row by dot product (cosine, since all vectors have unit
  length), rows of one code point collapse to the best one, and the top rows
  are kept. With 36 thousand rows of 384 floats this takes well under a
  millisecond, so a vector database or approximate index would add a
  dependency and a failure mode for nothing.
- Index: built on first use (about 6 s, 70 MB) into
  `${XDG_CACHE_HOME:-$HOME/.cache}/tellmeutf/semantic-index.bin`, written
  atomically and read back with a read-only mmap. The header records the format
  version, vector size, row count, model and vocabulary file sizes, a hash of
  the labels and the Unicode version; any mismatch, damage or truncation
  triggers a rebuild with a message on stderr, never a crash. The model files
  live in `${XDG_DATA_HOME:-$HOME/.local/share}/tellmeutf/semantic` (override:
  `TELLMEUTF_SEMANTIC_DIR`), installed by `scripts/fetch-semantic.sh`.
- Build: `-DTELLMEUTF_SEMANTIC=ON` links ONNX Runtime found under
  `ONNXRUNTIME_ROOT`. Without it a stub with the same interface
  (`src/semantic.hpp`) reports that the feature is not built in.

Output: a ranked list, best first, at most `--max` rows, in the candidate row
format with the cosine similarity (two decimals) and two spaces in front of
each row; no marker and no context window (`-C` has no effect). With `-s` (or
`-C 0`) only the best row is printed. Rows below the similarity floor (0.30,
`kMinSimilarity` in `src/main.cpp`) are dropped. The floor is low on purpose:
measured on 84 queries, right best matches scored 0.54 or more and unrelated
text anywhere from 0.35 to 0.71, so no threshold separates nonsense, and the
user judges by the gap between rows. If nothing is left the message is
`nothing is semantically close to '<query>'; ...` and the status is 1. Progress
messages (such as the index build) go to stderr as `tellmeutf: <message>`.

Status 5 means the feature is unavailable: not built in, model files missing,
the cache directory or index unusable, or the model runtime failed. The message
is the library's plain error text. Status 3 is never used for semantic results.

Measured with the real tool, `-S -m 10`, on the 84 descriptions of the original
spike (they were not used to set the weights): an acceptable character was in the top 5 for 80% of them with names
and aliases only, and for 92% once the CLDR texts were added (top 3: 79% to
88%, top 10: 83% to 93%; best match right: 67% to 68%). A word-overlap BM25
baseline measured in the same spike: 65% in the top 5. The tool's own name
matcher was not measured on that set. On a second set of 136 everyday names
and synonyms (the one the weights were tuned on) the best match was right 99
times before and 118 times after, and no acceptable character was in the top
10 for 22 before and 6 after.

Known limits: a word that needs outside knowledge or a CLDR keyword that is
missing still fails (`shift key`, `double quote`); a one-word query that is the
keyword of many characters returns those characters in an arbitrary order
(`cross` lists crossed fingers and flags among the crosses); similar names
(BLACK and WHITE variants) crowd the list.

### Options

```
-C N, --context=N   characters to show before and after (default 2)
-s, --single        show only the result (same as -C 0)
-n, --name          read the arguments as a name, even if they look like hex
-m N, --max=N       candidates to list for an ambiguous name (default 20)
-e, --exact         match names strictly; no approximate fallback
-S, --semantic      find characters by the meaning of a description (optional)
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
| 1 | a name matched nothing, not even approximately |
| 2 | usage error (bad option, invalid UTF-8, code point out of range, ...) |
| 3 | a name was ambiguous, or only approximate matches stood out too little |
| 4 | could not write the output |
| 5 | semantic search is unavailable (not built in, model files missing, index cache problem, runtime failure) |

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
data/cldr/annotations/en.xml  vendored CLDR English annotations (pinned release)
tools/gen_ucd.cpp       build-time generator: UCD text + CLDR annotations -> ucd_tables.cpp
src/ucd.hpp             PUBLIC CONTRACT of the lookup library (names, categories, widths)
src/ucd_tables.hpp      internal: shape of the generated tables
src/ucd.cpp             lookup + name search over the generated tables
src/fuzzy.cpp           approximate name retrieval (lazy index; ucd_internal.hpp shares helpers)
src/utf8.{hpp,cpp}      strict UTF-8 decode / encode
src/query.{hpp,cpp}     argument interpretation (pure; no UCD access)
src/options.{hpp,cpp}   command-line option parsing (pure; no globals)
src/render.{hpp,cpp}    glyph cell and row rendering (rows may carry a prefix column)
src/semantic.hpp        PUBLIC CONTRACT of the semantic search facade (exists in every build)
src/semantic_disabled.cpp  facade stub for builds without -DTELLMEUTF_SEMANTIC=ON
src/semantic.cpp        facade with the model: files, index cache, query (ON builds only)
src/semantic_embedder.* ONNX Runtime session, pooling, normalization (ON builds only)
src/semantic_index.*    vector index: brute-force search, on-disk form (no model runtime)
src/semantic_paths.*    data and cache directory resolution
src/wordpiece.*         BERT uncased WordPiece tokenizer (pure)
data/semantic/          vendored model vocabulary and provenance
scripts/fetch-semantic.sh  downloads and verifies the model and ONNX Runtime
src/main.cpp            wiring, I/O, exit status
tests/                  unit tests (tests/check.hpp), tests/e2e.sh
```

Targets: `tellmeutf_ucd` (static library: ucd.cpp + generated tables),
`tellmeutf_semantic_base` (tokenizer, index, paths), `tellmeutf_semantic` (the
facade; the model-backed one only with `-DTELLMEUTF_SEMANTIC=ON`),
`tellmeutf_core` (static library: utf8, query, options, render; links
`tellmeutf_ucd` and `tellmeutf_semantic`), `tellmeutf` (executable).

### Data pipeline

`gen_ucd` runs at build time (CMake `add_custom_command`) over the vendored
files and emits one C++ source file with the tables, so there is no runtime
file access and no download at build time. Configuration verifies every
vendored file against its `SHA256SUMS` (`data/ucd/`, `data/cldr/`). Updating
Unicode means running `scripts/update-ucd.sh <version>`, rebuilding, and fixing
whatever the generator rejects; updating CLDR means
`scripts/update-cldr.sh <release-tag>` (for example `release-48-2`), which
fetches `common/annotations/en.xml` from the CLDR repository at that tag.

The generator reports malformed input with file, line number and a plain
message, exits non-zero, and refuses unknown `First`/`Last` range names rather
than guessing (a new Unicode version adding a range must be taught explicitly).
It also refuses a CLDR annotation line it does not recognize, an unknown XML
entity, a second short name for one code point, and an annotation of a code
point that `UnicodeData.txt` does not list (a CLDR release newer than the
Unicode data).

Tables carry: for every explicitly listed code point its general category and
name; the `First`/`Last` ranges with their derivation rule; all aliases with
their type; the East Asian Wide/Fullwidth ranges; the CLDR annotations of
single code points (short name or keyword, normalized to name form, sorted by
code point with the short name first); the Unicode version (parsed from the
header of `NameAliases.txt`). Categories are emitted as
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
name), not column arrangement. Behavior that depends on the vendored CLDR data
is tested with a few stable facts (`backslash` is U+005C, `tick` lists U+2714
before U+27E4), not with whole result lists. Build trees live under `/workspace/build/`.
