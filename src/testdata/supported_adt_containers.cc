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

#include "llvm/include/llvm/ADT/DenseMap.h"
#include "llvm/include/llvm/ADT/DenseMapInfo.h"
#include "llvm/include/llvm/ADT/DenseSet.h"
#include "llvm/include/llvm/ADT/FoldingSet.h"
#include "llvm/include/llvm/ADT/Hashing.h"
#include "llvm/include/llvm/ADT/ImmutableMap.h"
#include "llvm/include/llvm/ADT/ImmutableSet.h"
#include "llvm/include/llvm/ADT/IntervalMap.h"
#include "llvm/include/llvm/ADT/MapVector.h"
#include "llvm/include/llvm/ADT/PagedVector.h"
#include "llvm/include/llvm/ADT/SetVector.h"
#include "llvm/include/llvm/ADT/SmallPtrSet.h"
#include "llvm/include/llvm/ADT/SmallSet.h"
#include "llvm/include/llvm/ADT/SmallString.h"
#include "llvm/include/llvm/ADT/SmallVector.h"
#include "llvm/include/llvm/ADT/SparseSet.h"
#include "llvm/include/llvm/ADT/StringMap.h"
#include "llvm/include/llvm/ADT/StringSet.h"
#include "llvm/include/llvm/ADT/TinyPtrVector.h"
#include "llvm/include/llvm/ADT/UniqueVector.h"
#include "llvm/include/llvm/Support/ErrorHandling.h"

struct A {
  uint64_t x;
  uint64_t y;
  template <typename H>
  friend H AbslHashValue(H h, const A &a) {
    return H::combine(std::move(h), a.x, a.y);
  }
  bool operator==(const A &other) const {
    if (x == other.x && y == other.y) return true;
    return false;
  }
  bool operator<(const A &other) const {
    if (x < other.x || y < other.y) return true;
    return false;
  }
  A(uint64_t x, uint64_t y) : x(x), y(y) {}
  A() : x(1), y(1) {}

  unsigned getSparseSetIndex() const { return x; }

  // Make A hashable.
  // friend ::llvm::hash_code hash_value(A arg);
};

/// Make A hashable.
// inline ::llvm::hash_code hash_value(A arg) { return ::llvm::hash_value(&arg);
// }

// inline ::llvm::hash_code hash_value(const A &a) {
//   return llvm::hash_value((unsigned)a.x);
// }

template <>
struct llvm::DenseMapInfo<A> {
  static inline A getEmptyKey() {
    A a;
    a.x = 0.0;
    return a;
  }
  static inline A getTombstoneKey() {
    A a;
    a.x = 0.0;
    return a;
  }
  static unsigned getHashValue(const A &Val) { return (unsigned)Val.x; }
  static bool isEqual(const A &LHS, const A &RHS) {
    return LHS.x == RHS.x && LHS.y == RHS.y;
  }
};

// Make A hashable.

// struct B : llvm::FoldingSetNode {
//   double x;
//   double y;
//   bool operator==(const B &other) const {
//     if (x == other.x && y == other.y) return true;
//     return false;
//   }
//   bool operator<(const B &other) const {
//     if (x < other.x || y < other.y) return true;
//     return false;
//   }
//   B(double x, double y) : x(x), y(y) {}
// };

typedef uint64_t B;

int main(int argc, char **argv) {
  // Vectors
  llvm::SmallVector<A, 1> As;
  As.push_back(A());
  As.push_back(A());

  As[0].x++;
  As[1].y++;

  llvm::PagedVector<A, 4096> page_As;
  page_As.resize(1);
  page_As[0] = A();

  llvm::TinyPtrVector<A *> tiny_As;
  tiny_As.push_back(&As[0]);

  // Strings

  llvm::StringRef s = "BIG STRING THAT ISHOPEFULLY ALLOCATED ON THE HEAP";

  llvm::SmallString<3> ss;
  ss.append(s.str());
  // llvm::Twine(s.str() + " " + s.str() + ss.str());

  llvm::SmallSet<A, 1> set_As;
  set_As.insert(A(1.0, 1.0));
  set_As.insert(A(2.0, 2.0));

  llvm::SmallPtrSet<A *, 1> set_ptr_As;
  set_ptr_As.insert(&As[0]);
  set_ptr_As.insert(&As[1]);

  llvm::StringSet<> set_ss;
  set_ss.insert(s.str());
  set_ss.insert(ss.str());

  llvm::DenseSet<A> dense_set_As;
  dense_set_As.insert(A(1.0, 1.0));
  dense_set_As.insert(A(2.0, 2.0));

  // llvm::SparseSet<A> sparse_set_As;
  // sparse_set_As.insert(A(1.0, 1.0));
  // sparse_set_As.insert(A(2.0, 2.0));

  // llvm::SparseMultiSet<A> sparse_multi_set_As;
  // sparse_multi_set_As.insert(A(1.0, 1.0));
  // sparse_multi_set_As.insert(A(2.0, 2.0));
  // sparse_multi_set_As.insert(A(2.0, 2.0));

  // llvm::FoldingSet<B> folding_set_As;
  // folding_set_As.InsertNode(new B(1));

  llvm::SetVector<A> set_vector_As;
  set_vector_As.insert(A(1.0, 1.0));
  set_vector_As.insert(A(2.0, 2.0));

  llvm::UniqueVector<A> unique_vector_As;
  unique_vector_As.insert(A(1.0, 1.0));
  unique_vector_As.insert(A(2.0, 2.0));

  llvm::ImmutableSet<int>::Factory f;
  auto immutable_set_As = f.getEmptySet();
  immutable_set_As = f.add(immutable_set_As, 1);
  immutable_set_As = f.add(immutable_set_As, 2);

  // Maps
  llvm::DenseMap<A, A> dense_map_As;
  dense_map_As[A(1.0, 1.0)] = A(2.0, 2.0);
  dense_map_As[A(2.0, 2.0)] = A(3.0, 3.0);

  llvm::StringMap<A> string_map_As;
  string_map_As[s.str()] = A(1.0, 1.0);
  string_map_As[ss.str()] = A(2.0, 2.0);

  // llvm::IntervalMap<int, A> interval_map_As;
  // interval_map_As.insert(0, 1, A(1.0, 1.0));
  // interval_map_As.insert(1, 2, A(2.0, 2.0));

  llvm::MapVector<A, A> map_vector_As;
  map_vector_As.insert({A(1.0, 1.0), A(2.0, 2.0)});
  map_vector_As.insert({A(2.0, 2.0), A(3.0, 3.0)});

  llvm::ImmutableMap<int, int>::Factory f_map;
  auto immutable_map = f_map.getEmptyMap();
  immutable_map = f_map.add(immutable_map, 1, 2);
  immutable_map = f_map.add(immutable_map, 1, 2);

  return 0;
}
