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

# Helpful script that runs on raw memprof yaml output files and demanges 
# all the callsite symbols for easier debugging. 
set -euo pipefail

if [[ $# -ne 1 ]]; then
    echo "Usage: $0 <file>" >&2
    exit 1
fi

file="$1"
tmpfile="$(mktemp)"

while IFS= read -r line; do
    echo "$line" >> "$tmpfile"
    if [[ "$line" =~ ^(.*SymbolName:[[:space:]]+)([^[:space:]]+) ]]; then
        prefix="${BASH_REMATCH[1]}"
        mangled="${BASH_REMATCH[2]}"
        demangled=$(c++filt "$mangled")
        echo "${prefix}${demangled}" >> "$tmpfile"
    fi
done < "$file"

mv "$tmpfile" "$file"
