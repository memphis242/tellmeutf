#!/usr/bin/env bash
# Downloads what the optional semantic search needs and prints the CMake flags.
#
#   scripts/fetch-semantic.sh [DIR]
#
# DIR defaults to ${XDG_DATA_HOME:-$HOME/.local/share}/tellmeutf/semantic and gets
#   DIR/onnxruntime/   ONNX Runtime release (MIT), headers and shared library
#   DIR/model.onnx     all-MiniLM-L6-v2, 8-bit quantized (Apache-2.0)
#   DIR/vocab.txt      the model's vocabulary, copied from data/semantic/
# Every download is checked against the SHA-256 pins below before it is used.
# Running the script again skips what is already in place and verified.
set -euo pipefail

ORT_VERSION=1.31.0
ORT_BASE="https://github.com/microsoft/onnxruntime/releases/download/v${ORT_VERSION}"
ORT_X64_SHA256=cc5c72baf5ae5c8238a6841f0897227be2d02826b9cf98eaf02fdefaeeb03a57
ORT_AARCH64_SHA256=c5b8b3cca31f3d643a3b313b8f42e2d0f16a4fcefd02cda343d97bd5306afaab

MODEL_REVISION=1110a243fdf4706b3f48f1d95db1a4f5529b4d41
MODEL_URL="https://huggingface.co/sentence-transformers/all-MiniLM-L6-v2/resolve/${MODEL_REVISION}/onnx/model_quint8_avx2.onnx"
MODEL_SHA256=b941bf19f1f1283680f449fa6a7336bb5600bdcd5f84d10ddc5cd72218a0fd21
VOCAB_SHA256=07eced375cec144d27c900241f3e339478dec958f92fddbc551f295c992038a3

repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
dir=${1:-"${XDG_DATA_HOME:-$HOME/.local/share}/tellmeutf/semantic"}

die() {
  echo "fetch-semantic: $*" >&2
  exit 1
}

case "$(uname -m)" in
  x86_64)  ort_name=onnxruntime-linux-x64-${ORT_VERSION}.tgz;     ort_sha=$ORT_X64_SHA256 ;;
  aarch64) ort_name=onnxruntime-linux-aarch64-${ORT_VERSION}.tgz; ort_sha=$ORT_AARCH64_SHA256 ;;
  *) die "no prebuilt ONNX Runtime pinned for $(uname -m); only x86_64 and aarch64 are supported" ;;
esac

sha_of() { sha256sum "$1" | cut -d' ' -f1; }

# download URL EXPECTED_SHA256 DESTINATION: fetches to a temporary file, verifies,
# then moves into place.
download() {
  local url=$1 expected=$2 destination=$3 partial
  partial=$(mktemp "${destination}.part.XXXXXX")
  if ! curl --fail --location --silent --show-error --output "$partial" "$url"; then
    rm -f "$partial"
    die "download failed: $url"
  fi
  local actual
  actual=$(sha_of "$partial")
  if [[ $actual != "$expected" ]]; then
    rm -f "$partial"
    die "checksum mismatch for $url (expected $expected, got $actual)"
  fi
  chmod 0644 "$partial"
  mv -f "$partial" "$destination"
}

mkdir -p "$dir"
dir=$(cd "$dir" && pwd)

# Vocabulary: from the repository, verified against the pin.
vocab_source="$repo/data/semantic/vocab.txt"
[[ -f $vocab_source ]] || die "missing $vocab_source"
[[ $(sha_of "$vocab_source") == "$VOCAB_SHA256" ]] || die "$vocab_source does not match its pinned checksum"
if [[ ! -f $dir/vocab.txt || $(sha_of "$dir/vocab.txt") != "$VOCAB_SHA256" ]]; then
  cp "$vocab_source" "$dir/vocab.txt"
  echo "installed $dir/vocab.txt"
fi

# Model.
if [[ -f $dir/model.onnx && $(sha_of "$dir/model.onnx") == "$MODEL_SHA256" ]]; then
  echo "model already in place: $dir/model.onnx"
else
  echo "downloading the model (23 MB)..."
  download "$MODEL_URL" "$MODEL_SHA256" "$dir/model.onnx"
  echo "installed $dir/model.onnx"
fi

# ONNX Runtime; .archive-sha256 records which verified archive is unpacked.
stamp="$dir/onnxruntime/.archive-sha256"
if [[ -f $stamp && $(cat "$stamp") == "$ort_sha" && -e $dir/onnxruntime/lib/libonnxruntime.so ]]; then
  echo "ONNX Runtime already in place: $dir/onnxruntime"
else
  echo "downloading ONNX Runtime ${ORT_VERSION} (11 MB)..."
  staging=$(mktemp -d "$dir/.staging.XXXXXX")
  trap 'rm -rf "$staging"' EXIT
  download "${ORT_BASE}/${ort_name}" "$ort_sha" "$staging/archive.tgz"
  mkdir "$staging/unpacked"
  tar -xzf "$staging/archive.tgz" -C "$staging/unpacked" --strip-components=1
  [[ -e $staging/unpacked/include/onnxruntime_cxx_api.h && -e $staging/unpacked/lib/libonnxruntime.so ]] ||
    die "the ONNX Runtime archive does not have the expected layout"
  echo "$ort_sha" > "$staging/unpacked/.archive-sha256"
  rm -rf "$dir/onnxruntime"
  mv "$staging/unpacked" "$dir/onnxruntime"
  echo "installed $dir/onnxruntime"
fi

cat <<EOF

Done. Build with the semantic search:

  cmake -S "$repo" -B build -DTELLMEUTF_SEMANTIC=ON -DONNXRUNTIME_ROOT="$dir/onnxruntime"

At run time the files are found in $dir by default; set TELLMEUTF_SEMANTIC_DIR to use another directory.
EOF
