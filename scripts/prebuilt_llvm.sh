#!/usr/bin/env bash

# Copyright 2025 Google LLC
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

# Helper script to compress and upload prebuilt LLVM install. Requires local llvm install already complete.

# Requires env.sh to be sourced
src_dir="${TOP_DIR}/third_party/llvm-project/install/"
#Get rid of some unnecessary bins to reduce size
extras=(
  clang-repl clang-check clang-scan-deps clang-nvlink-wrappper clang-refactor 
  clang-doc clang-rename clang-query clang-include-fixer clang-move
  clang-apply-replacements c-index-test pp-trace clang-extdef-mapping
  clang-change-namespace modularize hmaptool llc lli opt llvm-dis llvm-as
  llvm-link llvm-mc llvm-cov llvm-lto llvm-lto2
)
for b in "${extras[@]}"; do
  echo "Removing $b from prebuilt LLVM install"
  rm -rf "$src_dir/bin/$b"
done

tmpfile="$(mktemp --suffix=.tar.zst)"
echo "Storing prebuilt LLVM tarball to $tmpfile"

tar -I 'zstd -T0 --ultra -22 --long=29' -cf "$tmpfile" \
    -C "$(dirname "$src_dir")" "$(basename "$src_dir")"

aws s3 cp $tmpfile s3://memprof-prebuilt-llvm/llvm-install.tar.zst