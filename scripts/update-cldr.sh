#!/usr/bin/env bash
# Replaces the vendored CLDR English annotations with a given CLDR release.
#
#   scripts/update-cldr.sh release-48-2
#
# Downloads common/annotations/en.xml from the unicode-org/cldr repository at the
# given release tag, rewrites data/cldr/SHA256SUMS, and leaves updating the release
# named in data/cldr/README.md and the rebuild to you.
set -euo pipefail

if [[ $# -ne 1 || ! $1 =~ ^release-[0-9]+(-[0-9]+)*$ ]]; then
  echo "usage: $0 <cldr-release-tag, for example release-48-2>" >&2
  exit 2
fi

tag=$1
dest="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)/data/cldr"
file=annotations/en.xml

staging=$(mktemp -d)
trap 'rm -rf "$staging"' EXIT

mkdir -p "${staging}/annotations"
curl --fail --silent --show-error --location \
  "https://raw.githubusercontent.com/unicode-org/cldr/${tag}/common/${file}" --output "${staging}/${file}"
curl --fail --silent --show-error --location \
  "https://www.unicode.org/license.txt" --output "${staging}/LICENSE-UNICODE.txt"

if ! grep -q -F '<language type="en"/>' "${staging}/${file}"; then
  echo "$0: ${file} at ${tag} is not the English annotations file" >&2
  exit 1
fi

mkdir -p "${dest}/annotations"
cp "${staging}/${file}" "${dest}/${file}"
cp "${staging}/LICENSE-UNICODE.txt" "${dest}/LICENSE-UNICODE.txt"
(cd "${dest}" && sha256sum "${file}" > SHA256SUMS)
echo "Updated ${dest} to CLDR ${tag}"
echo "Next: update the release named in data/cldr/README.md, then rebuild."
