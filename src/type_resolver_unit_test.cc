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

#include <gtest/gtest.h>

#include <optional>
#include <string>

#include "absl/strings/string_view.h"
#include "re2/re2.h"
#include "src/type_resolver.h"

namespace devtools_crosstool_fdo_field_access {

namespace {

TEST(TypeResolverUnitTest, AbseilTypeStartsWith) {
  std::string s =
      "absl::lts_20250127::container_internal::raw_hash_set<absl::lts_20250127:"
      ":container_internal::FlatHashSetPolicy<A>, "
      "absl::lts_20250127::hash_internal::Hash<A>, std::equal_to<A>, "
      "std::allocator<A> "
      ">::resize_impl(absl::lts_20250127::container_internal::CommonFields&, "
      "unsigned long, "
      "absl::lts_20250127::container_internal::HashtablezInfoHandle)";
  auto m = DwarfTypeResolver::TypeStartsWith(s, "absl");

  ASSERT_TRUE(m.has_value());
  std::cout << *m << std::endl;
  EXPECT_EQ(*m, "absl");

  m = DwarfTypeResolver::TypeStartsWith(s, "absl::container_internal");
  ASSERT_TRUE(m.has_value());
  std::cout << *m << std::endl;
  EXPECT_EQ(*m, "absl::lts_20250127::container_internal");

  m = DwarfTypeResolver::TypeStartsWith(s, "absl::container_internal::");
  ASSERT_TRUE(m.has_value());
  std::cout << *m << std::endl;

  m = DwarfTypeResolver::TypeStartsWith(s, "absl::container_internal");
  ASSERT_TRUE(m.has_value());
  std::cout << *m << std::endl;
  EXPECT_EQ(*m, "absl::lts_20250127::container_internal");

  m = DwarfTypeResolver::TypeStartsWith(
      s, "absl::container_internal::raw_hash_set<");
  ASSERT_TRUE(m.has_value());
  std::cout << *m << std::endl;
  EXPECT_EQ(*m, "absl::lts_20250127::container_internal::raw_hash_set");

  s = "absl::lts_20250127::container_internal::btree<absl::lts_20250127::"
      "container_internal::map_params<A, unsigned int, std::less<A>, "
      "std::allocator<std::pair<const A, unsigned int> >, 256, true> >";

  m = DwarfTypeResolver::TypeStartsWith(s, "absl::container_internal::btree<");
  ASSERT_TRUE(m.has_value());
  std::cout << *m << std::endl;
  EXPECT_EQ(*m, "absl::lts_20250127::container_internal::btree");

  s = "absl::container_internal::btree<test>";

  m = DwarfTypeResolver::TypeStartsWith(s, "absl::container_internal::btree<");
  ASSERT_TRUE(m.has_value());
  std::cout << *m << std::endl;
  EXPECT_EQ(*m, "absl::container_internal::btree");
}

}  // namespace
}  // namespace devtools_crosstool_fdo_field_access