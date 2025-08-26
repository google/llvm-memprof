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
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

struct A {
  int x;
  int y;
};
static inline bool operator<(const A& l, const A& r) {
  return l.x < r.x || (l.x == r.x && l.y < r.y);
}
static inline bool operator==(const A& l, const A& r) {
  return l.x == r.x && l.y == r.y;
}
namespace std {
template <>
struct hash<A> {
  size_t operator()(const A& a) const noexcept {
    return (static_cast<size_t>(a.x) << 32) ^ static_cast<size_t>(a.y);
  }
};
}  // namespace std

int main() {
  std::vector<A> v;
  v.reserve(8);
  for (int i = 0; i < 8; ++i) v.push_back({i, i + 1});
  std::deque<A> dq;
  for (int i = 0; i < 8; ++i) dq.push_back({i, -i});
  std::list<A> lst;
  for (int i = 0; i < 8; ++i) lst.push_back({i, i});
  std::forward_list<A> fl;
  for (int i = 0; i < 8; ++i) fl.push_front({i, 2 * i});
  std::map<A, A> mp;
  for (int i = 0; i < 8; ++i) mp.emplace(A{i, i + 1}, A{i + 2, i + 3});
  std::multimap<A, A> mmp;
  for (int i = 0; i < 8; ++i) mmp.emplace(A{i, i}, A{i + 1, i + 1});
  std::set<A> st;
  for (int i = 0; i < 8; ++i) st.insert({i, 3 * i});
  std::multiset<A> mst;
  for (int i = 0; i < 8; ++i) mst.insert({i, -3 * i});
  std::unordered_map<A, A> ump;
  ump.reserve(16);
  for (int i = 0; i < 8; ++i) ump.emplace(A{i, i}, A{i + 1, i + 2});
  std::unordered_multimap<A, A> ummp;
  ummp.reserve(16);
  for (int i = 0; i < 8; ++i) ummp.emplace(A{i, i}, A{i, i});
  std::unordered_set<A> ust;
  ust.reserve(16);
  for (int i = 0; i < 8; ++i) ust.emplace(A{i, 5 * i});
  std::unordered_multiset<A> umst;
  umst.reserve(16);
  for (int i = 0; i < 8; ++i) umst.emplace(A{i, -5 * i});
  std::string s(4096, 'x');
  s += "y";
  std::queue<A> q;
  for (int i = 0; i < 8; ++i) q.push({i, i});
  std::stack<A> sk;
  for (int i = 0; i < 8; ++i) sk.push({i, i});
  std::priority_queue<int, std::vector<int>> pq;
  for (int i = 0; i < 8; ++i) pq.push(i);
  return 0;
}