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


prebuilt_llvm_url="https://memprof-prebuilt-llvm.s3.eu-north-1.amazonaws.com/llvm-install.tar.zst"

show_help() {
  cat <<EOF
Usage: $0 [options]

Options:
  -L, --no-llvm         Skip building the local LLVM/Clang toolchain
  -P, --prebuilt-llvm   Use a prebuilt LLVM/Clang tarball from a fixed URL
  -h, --help            Show this help message and exit

Without --no-llvm or --prebuilt-llvm, this script will build LLVM/Clang under
third_party/llvm-project/build.

EOF
  exit 0
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    -L|--no-llvm)
      skip_llvm=true
      shift
      ;;
    -P|--prebuilt-llvm)
      use_prebuilt_llvm=true
      shift
      ;;
    -h|--help)
      show_help
      ;;
    *)
      echo "Error: Unknown option '$1'"
      show_help
      ;;
  esac
done


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
python3-pip \
git \
libedit-dev \
libffi-dev \
libxml2-dev \
zlib1g-dev \
libncurses5-dev \
libtinfo-dev \
pkg-config \
selinux-utils \
unzip \
numactl # For Spec

# Install 6.4.0 bazel for field access tool.
log "Installing Bazel (6.4.0) for linux-x86 64..."
curl -LO "https://github.com/bazelbuild/bazel/releases/download/6.4.0/bazel-6.4.0-linux-x86_64"
chmod +x bazel-6.4.0-linux-x86_64
sudo mv bazel-6.4.0-linux-x86_64 /usr/local/bin/bazel

if ! command -v bazel &>/dev/null; then
    echo "ERROR: Bazel not found. Please install Bazel before running this script."
    exit 1
fi
log "Found bazel: $(bazel --version)"

# Install bazel 8.0.0 for fleetbench.
log "Installing Bazel (8.0.0) for linux-x86 64..."
curl -LO "https://github.com/bazelbuild/bazel/releases/download/8.0.0/bazel-8.0.0-linux-x86_64"
chmod +x bazel-8.0.0-linux-x86_64
sudo mv bazel-8.0.0-linux-x86_64 /usr/local/bin/bazel-8.0.0

if ! command -v bazel-8.0.0 &>/dev/null; then
    echo "ERROR: Bazel 8.0.0 not found. Please install Bazel 8.0.0 before running this script."
    exit 1
fi
log "Found bazel: $(bazel-8.0.0 --version)"



if [[ "$use_prebuilt_llvm" == true ]]; then
  echo "Downloading prebuilt LLVM from: $prebuilt_llvm_url"
  mkdir -p third_party/llvm-project/
  tmpfile=$(mktemp)
  curl -L "$prebuilt_llvm_url" -o "$tmpfile"

  echo "Extracting LLVM to third_party/llvm-project/install/"
  rm -rf third_party/llvm-project/install
  mkdir -p third_party/llvm-project/install
  tar -I 'zstd -d --memory=1024MB' -xf "$tmpfile" \
      -C third_party/llvm-project/install --strip-components=1
  rm -f "$tmpfile"
  echo "Prebuilt LLVM ready at third_party/llvm-project/install/"

# Build local llvm. This is also used in the bazel toolchain.
elif [ "$skip_llvm" = false ]; then
log "Building local LLVM/Clang toolchain..."
pushd "${TOP_DIR}/third_party/llvm-project/" >/dev/null
cmake -S llvm -B build -G Ninja \
-DLLVM_ENABLE_PROJECTS="clang;compiler-rt;lld" \
-DLLVM_ENABLE_RUNTIMES="libcxx;libcxxabi;libunwind" \
-DCMAKE_LINKER="lld" \
-DCMAKE_INSTALL_PREFIX="$(pwd)/install" \
-DCMAKE_BUILD_TYPE=Debug \
-DCMAKE_C_COMPILER=/usr/bin/clang \
-DCMAKE_CXX_COMPILER=/usr/bin/clang++ \
-DCMAKE_C_FLAGS="-O2" \
-DCMAKE_CXX_FLAGS="-O2" \
-DLLVM_ENABLE_LLD=On \
-DLLVM_TARGETS_TO_BUILD=host \

log "Starting LLVM build (make take a while)..."

ninja -j"$(nproc)" -C build install
popd >/dev/null
else
  log "Skipping LLVM build ( --no-llvm passed )"
fi

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

log "Wrote Bazel config to ${bazelrc}:"
cat ${TOP_DIR}/.bazelrc


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


# Generate unit tests
log "Generating unit tests..."
source env.sh
bash "${TOP_DIR}/src/testdata/update_dwarf.sh"

log "Initialization script complete."
