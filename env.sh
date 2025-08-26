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
export LLVM_BIN_DIR="${TOP_DIR}/third_party/llvm-project/install/bin"
export TESTSUITE_DIR="${TOP_DIR}/testsuite/"

#Change open file limit for DCPerf.
ulimit -n 65535

export PATH="${LLVM_BIN_DIR}:$PATH"
export CC="${LLVM_BIN_DIR}/clang"
export CXX="${LLVM_BIN_DIR}/clang++"
export LD="${LLVM_BIN_DIR}/lld"
export AR="${LLVM_BIN_DIR}/llvm-ar"
export NM="${LLVM_BIN_DIR}/llvm-nm"
export RANLIB="${LLVM_BIN_DIR}/llvm-ranlib"
export STRIP="${LLVM_BIN_DIR}/llvm-strip"

alias clang="${CC}"
alias clang++="${CXX}"
alias lld="${LD}"
alias llvm-ar="${AR}"
alias llvm-nm="${NM}"
alias llvm-ranlib="${RANLIB}"
alias llvm-profdata="${LLVM_BIN_DIR}/llvm-profdata"
alias llvm-dwarfdump="${LLVM_BIN_DIR}/llvm-dwarfdump"
alias readelf="${LLVM_BIN_DIR}/llvm-readelf"
alias objdump="${LLVM_BIN_DIR}/llvm-objdump"

# Python venv
if [ -f "${TOP_DIR}/.venv/bin/activate" ]; then
  source "${TOP_DIR}/.venv/bin/activate"
fi

