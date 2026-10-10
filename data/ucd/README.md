# Vendored Unicode Character Database

Unicode 18.0.0, downloaded from <https://www.unicode.org/Public/18.0.0/ucd/>.
The files are unmodified; `SHA256SUMS` records their checksums
(`sha256sum -c SHA256SUMS`). They are used under the Unicode License v3
(`LICENSE-UNICODE.txt`).

| file | used for |
|------|----------|
| `UnicodeData.txt` | code point names and general categories |
| `NameAliases.txt` | control names, abbreviations, corrections |
| `EastAsianWidth.txt` | terminal cell width (Wide and Fullwidth) |

To move to another release, run `scripts/update-ucd.sh <version>` and rebuild.
