# Semantic search data

Files for the optional semantic search (`-DTELLMEUTF_SEMANTIC=ON`).

## vocab.txt

The WordPiece vocabulary of `sentence-transformers/all-MiniLM-L6-v2` (BERT uncased, 30,522 lines,
one token per line, a token's id is its line number starting at 0).

- Source: https://huggingface.co/sentence-transformers/all-MiniLM-L6-v2/blob/1110a243fdf4706b3f48f1d95db1a4f5529b4d41/vocab.txt
- License: Apache-2.0
- SHA-256: `07eced375cec144d27c900241f3e339478dec958f92fddbc551f295c992038a3`

## Downloaded, not vendored

`scripts/fetch-semantic.sh` downloads these, verifies each against the checksum pinned in the script,
and installs them with a copy of `vocab.txt` into the data directory
(`${XDG_DATA_HOME:-$HOME/.local/share}/tellmeutf/semantic`, or `$TELLMEUTF_SEMANTIC_DIR` at run time).

| File | Source | License | SHA-256 |
| --- | --- | --- | --- |
| `model.onnx` (23,046,789 bytes) | `onnx/model_quint8_avx2.onnx` of `sentence-transformers/all-MiniLM-L6-v2` at revision `1110a243fdf4706b3f48f1d95db1a4f5529b4d41` | Apache-2.0 | `b941bf19f1f1283680f449fa6a7336bb5600bdcd5f84d10ddc5cd72218a0fd21` |
| ONNX Runtime 1.31.0 `onnxruntime-linux-x64-1.31.0.tgz` (11,408,003 bytes) | https://github.com/microsoft/onnxruntime/releases/tag/v1.31.0 | MIT | `cc5c72baf5ae5c8238a6841f0897227be2d02826b9cf98eaf02fdefaeeb03a57` |
| ONNX Runtime 1.31.0 `onnxruntime-linux-aarch64-1.31.0.tgz` (10,331,104 bytes) | same release | MIT | `c5b8b3cca31f3d643a3b313b8f42e2d0f16a4fcefd02cda343d97bd5306afaab` |

The model is a dynamically quantized (unsigned 8-bit weights) export of the original float model.
