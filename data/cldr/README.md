# Vendored CLDR annotations

CLDR release 48.2 (git tag `release-48-2` of <https://github.com/unicode-org/cldr>),
file `common/annotations/en.xml`, downloaded from
<https://raw.githubusercontent.com/unicode-org/cldr/release-48-2/common/annotations/en.xml>.
The file is unmodified; `SHA256SUMS` records its checksum
(`sha256sum -c SHA256SUMS`). It is used under the Unicode License v3
(SPDX `Unicode-3.0`, see `LICENSE-UNICODE.txt`).

Only this file is vendored. `annotationsDerived/en.xml` is not: it adds only emoji
sequences and 21 single code points.

| file | used for |
|------|----------|
| `annotations/en.xml` | CLDR short names and keywords, as extra search labels |

`tools/gen_ucd.cpp` keeps the annotations of single code points: the `tts` short
name and each keyword. It drops annotations of code point sequences, and any text
containing a non-ASCII character other than a Latin letter with a diacritic, which
is folded to its ASCII letter. Apostrophes are removed, other punctuation becomes
a space, and the text is upper-cased to match the form of Unicode names.

This release is paired with the Unicode 18.0.0 data in `data/ucd`. Annotations of
characters added in Unicode 18 are missing until a newer CLDR release is vendored.

To move to another release, run `scripts/update-cldr.sh <release-tag>`, update the
release named above, and rebuild.
