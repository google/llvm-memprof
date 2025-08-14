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

#!/bin/bash
export TOP_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" &> /dev/null && pwd)"

# Subdirs
export SCRIPTS_DIR="${TOP_DIR}/scripts"
export FLAMEGRAPH_DIR="${TOP_DIR}/third_party/FlameGraph"
export LLVM_BIN_DIR="${TOP_DIR}/third_party/llvm-project/build/bin"

#Change open file limit.
ulimit -n 65535

export PATH="${LLVM_BIN_DIR}:$PATH"
export CC="${LLVM_BIN_DIR}/clang"
export CXX="${LLVM_BIN_DIR}/clang++"
export LD="${LLVM_BIN_DIR}/lld"
export AR="${LLVM_BIN_DIR}/llvm-ar"
export NM="${LLVM_BIN_DIR}/llvm-nm"
export RANLIB="${LLVM_BIN_DIR}/llvm-ranlib"
export STRIP="${LLVM_BIN_DIR}/llvm-strip"

whereis clang

# Python venv
if [ -f "${TOP_DIR}/.venv/bin/activate" ]; then
  source "${TOP_DIR}/.venv/bin/activate"
fi

