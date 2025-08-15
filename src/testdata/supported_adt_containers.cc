// Copyright 2025 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/PagedVector.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/DenseMapInfo.h"
#include <cstdint>

struct A {
  std::uint64_t x;
  std::uint64_t y;
};

namespace llvm {
template <>
struct DenseMapInfo<A> {
  static inline A getEmptyKey() { return A{~0ull, ~0ull}; }
  static inline A getTombstoneKey() { return A{~0ull - 1, ~0ull - 1}; }
  static unsigned getHashValue(const A &v) {
    return static_cast<unsigned>((v.x ^ (v.y << 1)) ^ ((v.x >> 33) ^ (v.y >> 31)));
  }
  static bool isEqual(const A &a, const A &b) { return a.x == b.x && a.y == b.y; }
};
}

int main() {
  llvm::SmallVector<A, 8> sv;
  for (int i = 0; i < 20; ++i) sv.push_back({static_cast<std::uint64_t>(i+1), static_cast<std::uint64_t>(i+2)});

  llvm::PagedVector<A> pv;
  pv.resize(20);
  for (int i = 0; i < 20; ++i) pv[i] = {static_cast<std::uint64_t>(i+3), static_cast<std::uint64_t>(i+4)};

  llvm::DenseMap<A, unsigned> dm;
  for (int i = 0; i < 20; ++i) dm.insert({{static_cast<std::uint64_t>(i+5), static_cast<std::uint64_t>(i+6)}, static_cast<unsigned>(i)});

  llvm::DenseSet<A> ds;
  for (int i = 0; i < 20; ++i) ds.insert({static_cast<std::uint64_t>(i+7), static_cast<std::uint64_t>(i+8)});
}