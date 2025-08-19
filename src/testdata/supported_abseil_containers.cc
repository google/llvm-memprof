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

#include <cstdint>

#include "absl/container/btree_map.h"
#include "absl/container/btree_set.h"
#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"

struct A {
  std::uint64_t x;
  std::uint64_t y;
  friend bool operator==(const A& a, const A& b) {
    return a.x == b.x && a.y == b.y;
  }
  friend bool operator<(const A& a, const A& b) {
    return a.x < b.x || (a.x == b.x && a.y < b.y);
  }
};

template <typename H>
H AbslHashValue(H h, const A& a) {
  return H::combine(std::move(h), a.x, a.y);
}

int main() {
  absl::flat_hash_set<A> fhs;
  fhs.insert({1, 2});

  absl::flat_hash_map<A, unsigned> fhm;
  fhm.insert({{3, 4}, 42});

  absl::btree_set<A> bs;
  bs.insert({5, 6});

  absl::btree_map<A, unsigned> bm;
  bm.insert({{7, 8}, 7});

  absl::btree_multiset<A> bms;
  bms.insert({9, 10});

  absl::btree_multimap<A, unsigned> bmm;
  bmm.insert({{11, 12}, 11});
}