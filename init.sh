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
if [ -z "$TOP_DIR" ]; then
    echo "Error: Could not determine top-level git directory. Are you in the correct directory?" >&2
    exit 1
fi

log() {
    echo "[INFO] $*"
}

log "Initializing submodules..."
git submodule update --init --recursive

# Install dependencies
log "Updating apt package lists..."
sudo apt update

log "Installing system dependencies..."
sudo apt-get install -y \
clang \
lld \
lldb \
apt-transport-https \
curl \
gnupg \
cmake \
ninja-build \
build-essential \
python3 \
python3-distutils \
python3-venv \
git \
libedit-dev \
libffi-dev \
libxml2-dev \
zlib1g-dev \
libncurses5-dev \
libtinfo-dev \
pkg-config

# Install bazel
log "Adding Bazel GPG key and repository..."
curl -fsSL https://bazel.build/bazel-release.pub.gpg | gpg --dearmor > bazel-archive-keyring.gpg
sudo mv bazel-archive-keyring.gpg /usr/share/keyrings
echo "deb [arch=amd64 signed-by=/usr/share/keyrings/bazel-archive-keyring.gpg] \
https://storage.googleapis.com/bazel-apt stable jdk1.8" \
| sudo tee /etc/apt/sources.list.d/bazel.list >/dev/null

log "Updating apt (with Bazel repo)..."
sudo apt update

log "Installing Bazel (6.4.0)..."
sudo apt install -y bazel-6.4.0

# Build local llvm. This is also used in the bazel toolchain.
log "Building local LLVM/Clang toolchain..."
pushd "${TOP_DIR}/third_party/llvm-project/" >/dev/null
mkdir -p build
pushd build >/dev/null
cmake -GNinja \
-DLLVM_ENABLE_PROJECTS="clang;compiler-rt;lld" \
-DCMAKE_LINKER="lld" \
-DLLVM_ENABLE_LLD=On \
-DCMAKE_INSTALL_PREFIX="$(pwd)/install" \
-DCMAKE_BUILD_TYPE=Debug \
-DCMAKE_C_COMPILER=/usr/bin/clang \
-DCMAKE_CXX_COMPILER=/usr/bin/clang++ \
-DLLVM_TARGETS_TO_BUILD=host \
../llvm

ninja -j"$(nproc)"
popd >/dev/null
popd >/dev/null

# Setting up .bazelrc
log "Writing Bazel configuration to .bazelrc"

bazelrc="${TOP_DIR}/.bazelrc"
cat > "${bazelrc}" <<EOF
# Use our custom C/C++ toolchain suite by default
build --crosstool_top=//toolchain:clang_suite

# Point LLVM_ROOT to our local llvm-project checkout
build --define LLVM_ROOT=${TOP_DIR}/third_party/llvm-project

# Use C++20 standard
build --cxxopt="-std=c++20"
EOF

log "Wrote Bazel config to ${bazelrc}"


# Setting up python venv
log "Setting up Python virtual environment..."
if [ ! -d ".venv" ]; then
    echo "[INFO] Creating Python venv in .venv/"
    python3 -m venv .venv
fi

log "Installing python requirements..."
source "${TOP_DIR}/.venv/bin/activate"
pip install --upgrade pip
pip install -r requirements.txt

log "Initialization script complete."
