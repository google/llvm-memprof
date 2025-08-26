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

#include "dwarf_metadata_fetcher.h"

#include <stdio.h>

#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "binary_file_retriever.h"
#include "gtest/gtest.h"
#include "src/main/cpp/util/path.h"
#include "status_macros.h"
#include "test_status_macros.h"

constexpr const char *kDwarfMetadataFetchTestPath = "src/testdata/";

void TestFunctionality(const DwarfMetadataFetcher &test_target) {
  ASSERT_OK_AND_ASSIGN(const DwarfMetadataFetcher::TypeData *foo,
                       test_target.GetType("Foo"));
  ASSERT_EQ(foo->fields.size(), 5);
  EXPECT_EQ(foo->fields[0]->offset, 0);
  EXPECT_EQ(foo->fields[1]->offset, 4);
  EXPECT_EQ(foo->fields[1]->type_name, "char");
  EXPECT_EQ(foo->fields[2]->offset, 8);  // compiler auto padding
  EXPECT_EQ(foo->fields[3]->offset, 16);
  EXPECT_EQ(test_target.GetField("Foo", 0).value()->name, "a_");
  EXPECT_EQ(test_target.GetField("Foo", 3).value()->name, "a_");
  EXPECT_EQ(test_target.GetField("Foo", 4).value()->name, "bad_pad_");
  EXPECT_EQ(test_target.GetField("Foo", 8).value()->name, "b_");
  EXPECT_NOT_OK(test_target.GetField("Foo", 100));

  EXPECT_OK(test_target.GetType("Foo::FooInsider").status());
  EXPECT_OK(test_target.GetType("Bar<char>::BarPublicInsider").status());
  EXPECT_OK(test_target.GetType("Bar<Foo>::BarPublicInsider").status());
  EXPECT_OK(test_target.GetType("Bar<int>::BarPublicInsider").status());
  EXPECT_OK(
      test_target.GetType("Bar<AAA::BBB::CCC>::BarPublicInsider").status());
  EXPECT_OK(test_target.GetType("Bar<char>::BarPrivateInsider").status());
  EXPECT_OK(test_target.GetType("Bar<Foo>").status());
  EXPECT_OK(test_target.GetType("Bar<char>").status());
  EXPECT_OK(test_target.GetType("Bar<int>").status());
  EXPECT_OK(test_target.GetType("Bar<Foo>*").status());
  EXPECT_OK(test_target.GetType("myint32_t").status());
  EXPECT_OK(test_target.GetType("AAA::BBB::CCC").status());
  EXPECT_EQ(test_target.GetField("AAA::BBB::CCC", 60).value()->type_name,
            "int");
  EXPECT_OK(test_target.GetField("AAA::BBB::ChildFoo", 0));
  EXPECT_NOT_OK(test_target.GetType("Bar"));
}

TEST(DwarfMetadataFetcherTest, FetchAndProcessDebuggingInfo) {
  const std::string dwarf_path = blaze_util::JoinPath(
      kDwarfMetadataFetchTestPath, "dwarfmetadata_testdata.dwarf");
  {
    const std::string linker_build_id = "1001";
    std::unique_ptr<BinaryFileRetriever> retriever =
        BinaryFileRetriever::CreateMockRetriever(
            {{linker_build_id, dwarf_path}});

    DwarfMetadataFetcher test_target(std::move(retriever),
                                     ::testing::TempDir());
    ASSERT_OK(test_target.FetchWithPath({{linker_build_id, dwarf_path}},
                                        /*force_update_cache=*/true));
    TestFunctionality(test_target);
  }
}

TEST(DwarfMetadataFetcherTest, BasicTest) {
  const std::string raw_dwarf_dir = kDwarfMetadataFetchTestPath;
  ;
  const std::string dwarf_path =
      blaze_util::JoinPath(raw_dwarf_dir, "basic_type.dwarf");
  const std::string linker_build_id = "3393812c323bab6a";
  const std::string type_name = "A";
  // class A {
  // public:
  //   long int x;
  //   long int y;
  // };
  std::unique_ptr<BinaryFileRetriever> mock_retriever =
      BinaryFileRetriever::CreateMockRetriever({{linker_build_id, dwarf_path}});

  DwarfMetadataFetcher test_target(std::move(mock_retriever),
                                   ::testing::TempDir());
  ASSERT_OK(test_target.FetchWithPath({{linker_build_id, dwarf_path}},
                                      /*force_update_cache=*/true));
  EXPECT_OK(test_target.GetType(type_name));
}

TEST(DwarfMetadataFetcherTest, EmbeddedTest) {
  const std::string raw_dwarf_dir = kDwarfMetadataFetchTestPath;
  const std::string dwarf_path =
      blaze_util::JoinPath(raw_dwarf_dir, "embedded_type.dwarf");
  const std::string linker_build_id = "2adc2e18586c4f74";

  std::unique_ptr<BinaryFileRetriever> mock_retriever =
      BinaryFileRetriever::CreateMockRetriever({{linker_build_id, dwarf_path}});

  DwarfMetadataFetcher test_target(std::move(mock_retriever),
                                   ::testing::TempDir());
  ASSERT_OK(test_target.FetchWithPath({{linker_build_id, dwarf_path}},
                                      /*force_update_cache=*/true));
  ASSERT_OK_AND_ASSIGN(auto metadata, test_target.GetType("B"));

  // class A {
  //  public:
  //    long int x;
  //    long int y;
  // };

  // class B {
  // public:
  //   A a;
  // };

  EXPECT_EQ(metadata->name, "B");
  ASSERT_EQ(metadata->fields.size(), 1);
  EXPECT_EQ(metadata->fields[0]->name, "a");
  EXPECT_EQ(metadata->fields[0]->type_name, "A");
  EXPECT_EQ(metadata->size, 16);
}

TEST(DwarfMetadataFetcherTest, NameclashTest) {
  const std::string raw_dwarf_dir = kDwarfMetadataFetchTestPath;
  const std::string dwarf_path =
      blaze_util::JoinPath(raw_dwarf_dir, "namespace_clash.dwarf");
  const std::string linker_build_id = "ab47ad1c62a2e5b4";

  std::unique_ptr<BinaryFileRetriever> mock_retriever =
      BinaryFileRetriever::CreateMockRetriever({{linker_build_id, dwarf_path}});

  DwarfMetadataFetcher test_target(std::move(mock_retriever),
                                   ::testing::TempDir());
  ASSERT_OK(test_target.FetchWithPath({{linker_build_id, dwarf_path}},
                                      /*force_update_cache=*/true));

  // Two classes:
  // class A {
  // public:
  //   double x;
  //   double y;
  // };
  // In two namespaces name1 and name2.

  ASSERT_OK_AND_ASSIGN(auto metadata, test_target.GetType("name1::A"));
  EXPECT_EQ(metadata->name, "A");
  ASSERT_EQ(metadata->fields.size(), 2);
  EXPECT_EQ(metadata->fields[0]->name, "x");
  EXPECT_EQ(metadata->fields[0]->type_name, "long");

  ASSERT_OK_AND_ASSIGN(metadata, test_target.GetType("name2::A"));
  EXPECT_EQ(metadata->name, "A");
  ASSERT_EQ(metadata->fields.size(), 2);
  EXPECT_EQ(metadata->fields[0]->name, "x");
  EXPECT_EQ(metadata->fields[0]->type_name, "double");
}

TEST(DwarfMetadataFetcherTest, BasicStructTest) {
  const std::string raw_dwarf_dir = kDwarfMetadataFetchTestPath;
  const std::string dwarf_path =
      blaze_util::JoinPath(raw_dwarf_dir, "basic_struct_type.dwarf");
  const std::string linker_build_id = "e8f07bde09fd926a";

  std::unique_ptr<BinaryFileRetriever> mock_retriever =
      BinaryFileRetriever::CreateMockRetriever({{linker_build_id, dwarf_path}});

  DwarfMetadataFetcher test_target(std::move(mock_retriever),
                                   ::testing::TempDir());
  ASSERT_OK(test_target.FetchWithPath({{linker_build_id, dwarf_path}},
                                      /*force_update_cache=*/true));

  ASSERT_OK_AND_ASSIGN(auto metadata, test_target.GetType("A"));

  // struct A {
  //   long int x;
  //   long int y;
  // };

  EXPECT_EQ(metadata->name, "A");
  ASSERT_EQ(metadata->fields.size(), 2);
  EXPECT_EQ(metadata->fields[0]->name, "x");
  EXPECT_EQ(metadata->fields[0]->type_name, "long");
  EXPECT_EQ(metadata->fields[1]->name, "y");
  EXPECT_EQ(metadata->fields[1]->type_name, "long");
}

TEST(DwarfMetadataFetcherTest, UnwrapParameterizedStorageTest) {
  EXPECT_EQ(DwarfMetadataFetcher::UnwrapParameterizedStorage(
                "__gnu_cxx::__aligned_membuf<x>"),
            "x");
  EXPECT_EQ(DwarfMetadataFetcher::UnwrapParameterizedStorage(
                "__gnu_cxx::__aligned_membuf<x<y> >"),
            "x<y>");
  EXPECT_EQ(DwarfMetadataFetcher::UnwrapParameterizedStorage(
                "__gnu_cxx::__aligned_membuf<x::y<z> >"),
            "x::y<z>");
  EXPECT_EQ(DwarfMetadataFetcher::UnwrapParameterizedStorage(
                "__gnu_cxx::__aligned_membuf<x::y<z> >"),
            "x::y<z>");
  EXPECT_EQ(
      DwarfMetadataFetcher::UnwrapParameterizedStorage(
          "__gnu_cxx::__aligned_membuf<std::pair<const unsigned long, A> >"),
      "std::pair<const unsigned long, A>");

  EXPECT_EQ(DwarfMetadataFetcher::UnwrapParameterizedStorage("foo"),
            std::nullopt);
  EXPECT_EQ(DwarfMetadataFetcher::UnwrapParameterizedStorage(
                "not_membuf<std::pair<const unsigned long, A> >"),
            std::nullopt);
  EXPECT_EQ(DwarfMetadataFetcher::UnwrapParameterizedStorage(
                "__aligned_membuf<std::pair<const unsigned long, A> >"),
            std::nullopt);
}

TEST(DwarfMetadataFetcherTest, BasicMapTest) {
  const std::string raw_dwarf_dir = kDwarfMetadataFetchTestPath;
  const std::string dwarf_path =
      blaze_util::JoinPath(raw_dwarf_dir, "std_map_type.dwarf");
  const std::string linker_build_id = "55049bd39efcff2b";

  std::unique_ptr<BinaryFileRetriever> mock_retriever =
      BinaryFileRetriever::CreateMockRetriever({{linker_build_id, dwarf_path}});

  DwarfMetadataFetcher test_target(std::move(mock_retriever),
                                   ::testing::TempDir());
  ASSERT_OK(test_target.FetchWithPath({{linker_build_id, dwarf_path}},
                                      /*force_update_cache=*/true));

  ASSERT_OK_AND_ASSIGN(auto metadata, test_target.GetType("A"));

  // class A {
  // public:
  //   double x;
  //   double y;
  // };

  EXPECT_EQ(metadata->name, "A");
  ASSERT_EQ(metadata->fields.size(), 2);
  EXPECT_EQ(metadata->fields[0]->name, "x");
  EXPECT_EQ(metadata->fields[0]->type_name, "double");
  EXPECT_EQ(metadata->fields[1]->name, "y");
  EXPECT_EQ(metadata->fields[1]->type_name, "double");

  // Container type: std::map<long unsigned, A> As
  // has an internal structure type:

  // std::_Rb_tree_node<std::pair<const unsigned long, A> >
  // however, _Rb_tree_node will wrap the type in _aligned_membuf:

  // __aligned_membuf<std::pair<const unsigned long, A> >
  // which whill have type: char[24] instead of pair type

  // Pair will have fields:
  // _pair_base<const unsigned long, A> at offset 0
  // unsigned long at offset 0
  // A at offset 8

  ASSERT_OK_AND_ASSIGN(
      metadata, test_target.GetType(
                    "std::_Rb_tree_node<std::pair<const unsigned long, A> >"));
  EXPECT_EQ(metadata->name,
            "_Rb_tree_node<std::pair<const unsigned long, A> >");
  ASSERT_EQ(metadata->fields.size(), 2);
  EXPECT_EQ(metadata->fields[0]->name, "_Rb_tree_node_base");
  EXPECT_EQ(metadata->fields[0]->type_name, "std::_Rb_tree_node_base");
  EXPECT_EQ(metadata->fields[0]->offset, 0);
  EXPECT_EQ(metadata->fields[1]->name, "_M_storage");
  EXPECT_EQ(metadata->fields[1]->type_name,
            "std::pair<const unsigned long, A>");
  EXPECT_EQ(metadata->fields[1]->offset, 32);
  ASSERT_OK_AND_ASSIGN(
      metadata, test_target.GetType("std::pair<const unsigned long, A>"));

  EXPECT_EQ(metadata->name, "pair<const unsigned long, A>");
  ASSERT_EQ(metadata->fields.size(), 3);
  ASSERT_EQ(metadata->offset_idx.size(), 2);
  auto it = metadata->offset_idx.begin();
  EXPECT_EQ(it->second.size(), 2);
  EXPECT_TRUE(it->second.contains(0));
  EXPECT_TRUE(it->second.contains(1));
  it++;
  EXPECT_EQ(it->second.size(), 1);
  EXPECT_TRUE(it->second.contains(2));
  EXPECT_EQ(metadata->fields[0]->type_name,
            "std::__pair_base<const unsigned long, A>");
  EXPECT_EQ(metadata->fields[0]->offset, 0);
  EXPECT_EQ(metadata->fields[1]->type_name, "unsigned long");
  EXPECT_EQ(metadata->fields[1]->offset, 0);
  EXPECT_EQ(metadata->fields[2]->type_name, "A");
  EXPECT_EQ(metadata->fields[2]->offset, 8);
}

// This tests if we can resolve full field types names that are in
// namespaces.
// This includes type fields that have typedef DIE in between the root type
// definition and the "short hand" type name. For example, std::string is
// just a
// typedef for 'std::__cxx11::basic_string<char, std::char_traits<char>,
// std::allocator<char> >'.
TEST(DwarfMetadataFetcherTest, NamespaceFieldTest) {
  const std::string raw_dwarf_dir = kDwarfMetadataFetchTestPath;
  const std::string dwarf_path =
      blaze_util::JoinPath(raw_dwarf_dir, "namespace_field.dwarf");
  const std::string linker_build_id = "ae2c97a1e1741809";

  std::unique_ptr<BinaryFileRetriever> mock_retriever =
      BinaryFileRetriever::CreateMockRetriever({{linker_build_id, dwarf_path}});

  DwarfMetadataFetcher test_target(std::move(mock_retriever),
                                   ::testing::TempDir());
  ASSERT_OK(test_target.FetchWithPath({{linker_build_id, dwarf_path}},
                                      /*force_update_cache=*/true));
  // namespace n1 {
  //     struct B {
  //         long int x;
  //         B() : x(1){}
  //     };
  // }

  // struct A {
  //   long int x;
  //   std::string y;
  //   n1::B b;
  //   A() : x(1), y(""), b() {}
  // };

  ASSERT_OK_AND_ASSIGN(auto metadata, test_target.GetType("A"));
  EXPECT_EQ(metadata->name, "A");
  ASSERT_EQ(metadata->fields.size(), 3);
  EXPECT_EQ(metadata->fields[0]->name, "x");
  EXPECT_EQ(metadata->fields[0]->type_name, "long");
  EXPECT_EQ(metadata->fields[1]->name, "y");
  EXPECT_EQ(metadata->fields[1]->type_name,
            "std::__cxx11::basic_string<char, std::char_traits<char>, "
            "std::allocator<char> >");
  EXPECT_EQ(metadata->fields[2]->name, "b");
  EXPECT_EQ(metadata->fields[2]->type_name, "n1::B");
  EXPECT_EQ(metadata->fields[2]->type_name, "n1::B");

  ASSERT_OK_AND_ASSIGN(metadata, test_target.GetType("n1::B"));
  EXPECT_EQ(metadata->name, "B");
  ASSERT_EQ(metadata->fields.size(), 1);
  EXPECT_EQ(metadata->fields[0]->name, "x");
  EXPECT_EQ(metadata->fields[0]->type_name, "long");
  ASSERT_OK_AND_ASSIGN(
      metadata,
      test_target.GetType("std::__cxx11::basic_string<char, "
                          "std::char_traits<char>, std::allocator<char> >"));
  ASSERT_EQ(metadata->fields.size(), 3);
  EXPECT_EQ(metadata->fields[0]->name, "_M_dataplus");
  EXPECT_EQ(metadata->fields[0]->type_name,
            "std::__cxx11::basic_string<char, std::char_traits<char>, "
            "std::allocator<char> >::_Alloc_hider");
  EXPECT_EQ(metadata->fields[0]->offset, 0);
  EXPECT_EQ(metadata->fields[1]->name, "_M_string_length");
  EXPECT_EQ(metadata->fields[1]->type_name, "unsigned long");
  EXPECT_EQ(metadata->fields[1]->offset, 8);
  EXPECT_EQ(metadata->fields[2]->name, "");
  EXPECT_EQ(metadata->fields[2]->offset, 16);
  EXPECT_EQ(metadata->fields[2]->type_name,
            "std::__cxx11::basic_string<char, std::char_traits<char>, "
            "std::allocator<char> >::Anon_197");
}

// This tests if we can resolve union types. This is a special case, because
// we
// don't care about the internal of the union as of know. All we care about
// is
// detecting unions, and getting the size of the union type.
TEST(DwarfMetadataFetcherTest, UnionTypeTest) {
  const std::string raw_dwarf_dir = kDwarfMetadataFetchTestPath;
  const std::string dwarf_path =
      blaze_util::JoinPath(raw_dwarf_dir, "union_type.dwarf");
  const std::string linker_build_id = "a7e20eefbd6e7371";

  std::unique_ptr<BinaryFileRetriever> mock_retriever =
      BinaryFileRetriever::CreateMockRetriever({{linker_build_id, dwarf_path}});

  DwarfMetadataFetcher test_target(std::move(mock_retriever),
                                   ::testing::TempDir());
  ASSERT_OK(test_target.FetchWithPath({{linker_build_id, dwarf_path}},
                                      /*force_update_cache=*/true));

  ASSERT_OK_AND_ASSIGN(auto metadata, test_target.GetType("A"));
  EXPECT_EQ(metadata->name, "A");
  EXPECT_EQ(metadata->data_type, DwarfMetadataFetcher::DataType::UNION);
  EXPECT_EQ(metadata->size, 8);
  ASSERT_OK_AND_ASSIGN(metadata, test_target.GetType("X"));
  EXPECT_EQ(metadata->name, "X");
  EXPECT_EQ(metadata->size, 8);
  ASSERT_EQ(metadata->fields.size(), 1);
  EXPECT_EQ(metadata->fields[0]->name, "a");
  EXPECT_EQ(metadata->fields[0]->type_name, "A");
  EXPECT_EQ(metadata->fields[0]->offset, 0);
}

// This tests if we can resolve record types with embedded arrays.
TEST(DwarfMetadataFetcherTest, ArrayTypeTest) {
  const std::string raw_dwarf_dir = kDwarfMetadataFetchTestPath;
  const std::string dwarf_path =
      blaze_util::JoinPath(raw_dwarf_dir, "array_type.dwarf");
  const std::string linker_build_id = "759929e945cf3888";

  std::unique_ptr<BinaryFileRetriever> mock_retriever =
      BinaryFileRetriever::CreateMockRetriever({{linker_build_id, dwarf_path}});

  DwarfMetadataFetcher test_target(std::move(mock_retriever),
                                   ::testing::TempDir());
  ASSERT_OK(test_target.FetchWithPath({{linker_build_id, dwarf_path}},
                                      /*force_update_cache=*/true));
  ASSERT_OK_AND_ASSIGN(auto metadata, test_target.GetType("A"));
  // struct A {
  //   long int x;
  //   int y[24];
  //   A() : x(1), y() {}
  // };

  // int main(int argc, char** argv) {
  //   A* a = new A;
  //   return 0;
  // }
  EXPECT_EQ(metadata->name, "A");
  ASSERT_EQ(metadata->fields.size(), 2);
  EXPECT_EQ(metadata->fields[0]->name, "x");
  EXPECT_EQ(metadata->fields[1]->name, "y");
  EXPECT_EQ(metadata->fields[0]->type_name, "long");
  EXPECT_EQ(metadata->fields[1]->type_name, "int[24]");
}

// This tests if we can resolve enum types. For our case, we don't care about
// whether or not we can resolve to an enum type. In most cases the enum will
// have base type unsigned int. This test just makes sure we can resolve to
// the
// base type.
TEST(DwarfMetadataFetcherTest, EnumTypeTest) {
  const std::string raw_dwarf_dir = kDwarfMetadataFetchTestPath;
  const std::string dwarf_path =
      blaze_util::JoinPath(raw_dwarf_dir, "enum_type.dwarf");
  const std::string linker_build_id = "86ba6a44e46f1f6d";

  std::unique_ptr<BinaryFileRetriever> mock_retriever =
      BinaryFileRetriever::CreateMockRetriever({{linker_build_id, dwarf_path}});

  DwarfMetadataFetcher test_target(std::move(mock_retriever),
                                   ::testing::TempDir());
  ASSERT_OK(test_target.FetchWithPath({{linker_build_id, dwarf_path}},
                                      /*force_update_cache=*/true));
  ASSERT_OK_AND_ASSIGN(auto metadata, test_target.GetType("A"));
  // enum E { X = 1, Y = 2, Z = 3 };
  // class A {
  //  public:
  //   E e;
  //   double x;
  // };
  EXPECT_EQ(metadata->name, "A");
  EXPECT_EQ(metadata->size, 16);
  ASSERT_EQ(metadata->fields.size(), 2);
  EXPECT_EQ(metadata->fields[0]->name, "e");
  EXPECT_EQ(metadata->fields[0]->type_name, "unsigned int");
  EXPECT_EQ(metadata->fields[1]->name, "x");
  EXPECT_EQ(metadata->fields[1]->type_name, "double");
}

// This tests if we can correctly resolve typedefs that refer to types in
// another namespace.
TEST(DwarfMetadataFetcherTest, NamespaceTypeDef) {
  const std::string raw_dwarf_dir = kDwarfMetadataFetchTestPath;
  const std::string dwarf_path =
      blaze_util::JoinPath(raw_dwarf_dir, "namespace_typedef.dwarf");
  const std::string linker_build_id = "ed2adf11f522b4c4";

  std::unique_ptr<BinaryFileRetriever> mock_retriever =
      BinaryFileRetriever::CreateMockRetriever({{linker_build_id, dwarf_path}});

  DwarfMetadataFetcher test_target(std::move(mock_retriever),
                                   ::testing::TempDir());
  ASSERT_OK(test_target.FetchWithPath({{linker_build_id, dwarf_path}},
                                      /*force_update_cache=*/true));
  // namespace n1 {
  // struct A {
  //   double x;
  //   double y;
  // };
  // } // namespace n1

  // namespace n2 {
  // typedef n1::A B;
  // } // namespace n2
  ASSERT_OK_AND_ASSIGN(auto metadata, test_target.GetType("n2::B"));
  EXPECT_EQ(metadata->name, "A");
  EXPECT_EQ(metadata->size, 16);
  ASSERT_EQ(metadata->fields.size(), 2);
  EXPECT_EQ(metadata->fields[0]->name, "x");
  EXPECT_EQ(metadata->fields[0]->type_name, "double");
  EXPECT_EQ(metadata->fields[1]->name, "y");
  EXPECT_EQ(metadata->fields[1]->type_name, "double");
}
