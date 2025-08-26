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

#include "absl/container/btree_map.h"
#include "absl/container/btree_set.h"
#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/container/node_hash_map.h"
#include "absl/container/node_hash_set.h"


struct A {
  uint64_t x0;
  uint64_t x1;
  uint64_t x2;
  uint64_t x3;
  uint64_t x4;
  uint64_t x5;
  uint64_t x6;
  uint64_t x7;
  uint64_t x8;
  uint64_t x9;
  uint64_t x10;
  uint64_t x11;

  template <typename H> friend H AbslHashValue(H h, const A &ml) {
    return H::combine(std::move(h), ml.x0, ml.x1, ml.x2, ml.x3, ml.x4, ml.x5,
                      ml.x6, ml.x7, ml.x8, ml.x9, ml.x10, ml.x11);
  }
  bool operator==(const A &other) const {
    if (x0 == other.x0 && x1 == other.x1 && x2 == other.x2 && x3 == other.x3 &&
        x4 == other.x4 && x5 == other.x5 && x6 == other.x6 && x7 == other.x7 &&
        x8 == other.x8 && x9 == other.x9 && x10 == other.x10 &&
        x11 == other.x11)
      return true;
    return false;
  }

  bool operator<(const A &other) const {
    if (x0 < other.x0 || x1 < other.x1 || x2 < other.x2 || x3 < other.x3 ||
        x4 < other.x4 || x5 < other.x5 || x6 < other.x6 || x7 < other.x7 ||
        x8 < other.x8 || x9 < other.x9 || x10 < other.x10 || x11 < other.x11)
      return true;
    return false;
  }

  A(int i) {
    x0 = i * 101;
    x1 = i * 102;
    x2 = i * 103;
    x3 = i * 104;
    x4 = i * 105;
    x5 = i * 106;
    x6 = i * 107;
    x7 = i * 108;
    x8 = i * 109;
    x9 = i * 110;
    x10 = i * 111;
    x11 = i * 112;
  }
  A() {
    x0 = 101;
    x1 = 102;
    x2 = 103;
    x3 = 104;
    x4 = 105;
    x5 = 106;
    x6 = 107;
    x7 = 108;
    x8 = 109;
    x9 = 110;
    x10 = 111;
    x11 = 112;
  }
};

struct B {
  int x;
  int y;
  template <typename H> friend H AbslHashValue(H h, const B &b) {
    return H::combine(std::move(h), b.x, b.y);
  }
  bool operator==(const B &other) const {
    if (x == other.x && y == other.y)
      return true;
    return false;
  }

  bool operator<(const B &other) const {
    if (x < other.x && y < other.y)
      return true;
    return false;
  }

  B() : x(1.0), y(1.0) {}
};


int main() {
  absl::flat_hash_set<A> set1;
  set1.insert(A());
  absl::node_hash_set<A> set2;
  set2.insert(A());
  absl::flat_hash_map<A, A> map1;
  map1.insert({A(), A()});
  absl::node_hash_map<A, A> map2;
  map2.insert({A(), A()});
  absl::btree_set<A> set3;
  set3.insert(A());
  absl::btree_multiset<A> set4;
  set4.insert(A());
  absl::btree_map<A, A> map3;
  map3.insert({A(), A()});
  absl::btree_multimap<A, A> map4;
  map4.insert({A(), A()});
  absl::FixedArray<A> arr(10);
  arr[0] = A();
  absl::InlinedVector<A, 1> vector;
  vector.push_back(A());
  vector.push_back(A());
  vector.push_back(A());
}


