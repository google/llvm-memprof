#!/usr/bin/env bash

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
    if [[ "$line" =~ ^(.*SymbolName:[[:space:]]+)([^[:space:]]+) ]]; then
        prefix="${BASH_REMATCH[1]}"
        mangled="${BASH_REMATCH[2]}"
        demangled=$(c++filt "$mangled")
        echo "${prefix}${demangled}" >> "$tmpfile"
    else
        echo "$line" >> "$tmpfile"
    fi
done < "$file"

mv "$tmpfile" "$file"