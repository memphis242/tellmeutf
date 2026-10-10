#!/usr/bin/env bash
# Replaces the vendored Unicode Character Database files with a given release.
#
#   scripts/update-ucd.sh 18.0.0
#
# Downloads the files named below from unicode.org, rewrites data/ucd/SHA256SUMS,
# and leaves the rebuild (and any generator complaints about new ranges) to you.
set -euo pipefail

if [[ $# -ne 1 || ! $1 =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
  echo "usage: $0 <unicode-version, for example 18.0.0>" >&2
  exit 2
fi

version=$1
dest="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)/data/ucd"
files=(UnicodeData.txt NameAliases.txt EastAsianWidth.txt)

staging=$(mktemp -d)
trap 'rm -rf "$staging"' EXIT

for file in "${files[@]}"; do
  curl --fail --silent --show-error --location \
    "https://www.unicode.org/Public/${version}/ucd/${file}" --output "${staging}/${file}"
done
curl --fail --silent --show-error --location \
  "https://www.unicode.org/license.txt" --output "${staging}/LICENSE-UNICODE.txt"

for file in "${files[@]}"; do
  if ! head -n 1 "${staging}/${file}" | grep -q -- "-${version}"; then
    # UnicodeData.txt carries no version header; the other two do.
    if [[ $file != UnicodeData.txt ]]; then
      echo "$0: ${file} does not declare version ${version}" >&2
      exit 1
    fi
  fi
done

cp "${staging}"/*.txt "${dest}/"
(cd "${dest}" && sha256sum "${files[@]}" > SHA256SUMS)
echo "Updated ${dest} to Unicode ${version}"
