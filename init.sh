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
TOP_DIR=$(git rev-parse --show-toplevel)

git submodule update --init --recursive

# Build local llvm
cd "${TOP_DIR}/third_party/llvm-project/"
mkdir -p "build"
cd "build"
cmake -GNinja -DLLVM_ENABLE_PROJECTS="clang;compiler-rt;lld" \
-DCMAKE_LINKER="lld" -DLLVM_ENABLE_LLD=On -DCMAKE_INSTALL_PREFIX=`pwd`/install \
-DCMAKE_BUILD_TYPE=Debug -DCMAKE_C_COMPILER=/usr/bin/clang \
-DCMAKE_CXX_COMPILER=/usr/bin/clang++ \
-DLLVM_TARGETS_TO_BUILD=host ../llvm

ninja -j $(nproc --all);

# This step is required to make the bazel build cross toolchain to work with 
# local llvm install in third_party
sudo ln -s ${TOP_DIR}/third_party/llvm-project/build/ /usr/llvm-memprof

# Setup testing infrastructure
source ${TOP_DIR}/env.sh
bash ${TOP_DIR}/src/testdata/update_dwarf.sh
