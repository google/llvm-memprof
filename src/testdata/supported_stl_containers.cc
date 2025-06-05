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

#include <deque>
#include <forward_list>
#include <list>
#include <map>
#include <queue>
#include <set>
#include <stack>
#include <stdint.h>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

struct A {
  uint64_t x;
  uint64_t y;

  bool operator==(const A &other) const {
    if (x == other.x && y == other.y)
      return true;
    return false;
  }

  bool operator<(const A &other) const {
    if (x < other.x)
      return true;
    return false;
  }

  size_t operator()(const A &a) const noexcept { return a.x; };
};

template <> struct std::hash<A> {
  std::size_t operator()(const A &a) const noexcept { return a(a); }
};

int main() {
  std::vector<A> vector_A;
  vector_A.push_back(A());

  std::deque<A> deque_A;
  deque_A.push_back(A());

  std::set<A> set_A;
  set_A.insert(A());

  std::multiset<A> multiset_A;
  multiset_A.insert(A());

  std::unordered_set<A> unordered_set_A(0);
  unordered_set_A.insert(A());

  std::unordered_multiset<A> unordered_multiset_A(0);
  unordered_multiset_A.insert(A());

  std::forward_list<A> fwd_list_A;
  fwd_list_A.push_front(A());

  std::list<A> list_A;
  list_A.push_back(A());

  std::stack<A> stack_A;
  stack_A.push(A());

  std::queue<A> queue_A;
  queue_A.push(A());

  std::priority_queue<A> p_queue_A;
  p_queue_A.push(A());

  std::map<A, A> map_A;
  map_A.insert({A(), A()});

  std::multimap<A, A> multimap_A;
  multimap_A.insert({A(), A()});

  std::unordered_map<A, A> unordered_map_A;
  unordered_map_A.insert({A(), A()});

  std::unordered_multimap<A, A> unordered_multimap_A;
  unordered_multimap_A.insert({A(), A()});

  std::string string =
      "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx";
}