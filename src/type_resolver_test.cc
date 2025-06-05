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

#include "type_resolver.h"

#include <google/protobuf/text_format.h>

#include <cstdint>
#include <memory>
#include <string>
#include <utility>

#include "absl/status/status.h"
#include "absl/strings/string_view.h"
#include "binary_file_retriever.h"
#include "gtest/gtest.h"
#include "src/dwarf_metadata_fetcher.h"
#include "src/object_layout.pb.h"
#include "src/type_tree.h"
#include "src/main/cpp/util/path.h"
#include "status_macros.h"
#include "test_status_macros.h"

namespace devtools_crosstool_fdo_field_access {

namespace {

constexpr uint64_t kDummyLineColNo = 0;

constexpr const char *kTypeResolverTestPath = "src/testdata";

// Basic type test. Checks if we can resolve a simple type into a full
// TypeTree.
TEST(TypeResolverTest, BasicTest) {
  const std::string dwarf_path =
      blaze_util::JoinPath(kTypeResolverTestPath, "basic_type.dwarf");
  const std::string linker_build_id = "056d411c166d583f";
  const std::string type_name = "A";

  std::unique_ptr<BinaryFileRetriever> mock_retriever =
      BinaryFileRetriever::CreateMockRetriever({{linker_build_id, dwarf_path}});

  auto dwarf_metadata_fetcher = std::make_unique<DwarfMetadataFetcher>(
      std::move(mock_retriever), ::testing::TempDir(), true);
  ASSERT_OK(
      dwarf_metadata_fetcher->FetchWithPath({{linker_build_id, dwarf_path}},
                                            /*force_update_cache=*/true));
  auto type_resolver =
      std::make_unique<DwarfTypeResolver>(std::move(dwarf_metadata_fetcher));

  ASSERT_OK_AND_ASSIGN(std::unique_ptr<TypeTree> type_tree,
                       type_resolver->ResolveTypeFromTypeName(type_name));
  // class A {
  //  public:
  //   long int x;
  //   long int y;
  // };
  EXPECT_TRUE(type_tree->Verify(/*verify_verbose=*/true));
  EXPECT_EQ(type_tree->Root()->GetTypeName(), type_name);
  EXPECT_EQ(type_tree->Root()->GetSizeBytes(), 16);
  EXPECT_EQ(type_tree->Root()->GetOffsetBytes(), 0);
  ASSERT_EQ(type_tree->Root()->NumChildren(), 2);
  EXPECT_EQ(type_tree->Root()->GetChild(0)->GetTypeName(), "long");
  EXPECT_EQ(type_tree->Root()->GetChild(1)->GetTypeName(), "long");
  EXPECT_EQ(type_tree->Root()->GetChild(0)->GetSizeBytes(), 8);
  EXPECT_EQ(type_tree->Root()->GetChild(1)->GetSizeBytes(), 8);
  EXPECT_EQ(type_tree->Root()->GetChild(0)->GetOffsetBytes(), 0);
  EXPECT_EQ(type_tree->Root()->GetChild(1)->GetOffsetBytes(), 8);
}

// Check if we can resolve a type that is embedded in another type.
TEST(TypeResolverTest, EmbeddedTest) {
  const std::string dwarf_path =
      blaze_util::JoinPath(kTypeResolverTestPath, "embedded_type.dwarf");
  const std::string linker_build_id = "79f61a072f0c57d1";
  const std::string type_name = "B";

  std::unique_ptr<BinaryFileRetriever> mock_retriever =
      BinaryFileRetriever::CreateMockRetriever({{linker_build_id, dwarf_path}});
  auto dwarf_metadata_fetcher = std::make_unique<DwarfMetadataFetcher>(
      std::move(mock_retriever), ::testing::TempDir());
  ASSERT_OK(
      dwarf_metadata_fetcher->FetchWithPath({{linker_build_id, dwarf_path}},
                                            /*force_update_cache=*/true));
  auto type_resolver =
      std::make_unique<DwarfTypeResolver>(std::move(dwarf_metadata_fetcher));

  ASSERT_OK_AND_ASSIGN(std::unique_ptr<TypeTree> type_tree,
                       type_resolver->ResolveTypeFromTypeName(type_name));

  EXPECT_TRUE(type_tree->Verify(/*verify_verbose=*/true));
  // class A {
  //  public:
  //   long int x;
  //   long int y;
  // };
  // class B {
  //  public:
  //   A a;
  // };
  EXPECT_EQ(type_tree->Root()->GetTypeName(), type_name);
  EXPECT_EQ(type_tree->Root()->GetTypeName(), type_name);
  EXPECT_EQ(type_tree->Root()->GetSizeBytes(), 16);
  EXPECT_EQ(type_tree->Root()->GetOffsetBytes(), 0);
  ASSERT_EQ(type_tree->Root()->NumChildren(), 1);
  ASSERT_EQ(type_tree->Root()->GetChild(0)->NumChildren(), 2);
  EXPECT_EQ(type_tree->Root()->GetChild(0)->GetTypeName(), "A");
  EXPECT_EQ(type_tree->Root()->GetChild(0)->GetChild(0)->GetTypeName(), "long");
  EXPECT_EQ(type_tree->Root()->GetChild(0)->GetChild(1)->GetTypeName(), "long");
  EXPECT_EQ(type_tree->Root()->GetChild(0)->GetChild(0)->GetSizeBytes(), 8);
  EXPECT_EQ(type_tree->Root()->GetChild(0)->GetChild(1)->GetSizeBytes(), 8);
  EXPECT_EQ(type_tree->Root()->GetChild(0)->GetChild(0)->GetOffsetBytes(), 0);
  EXPECT_EQ(type_tree->Root()->GetChild(0)->GetChild(1)->GetOffsetBytes(), 8);

  // Check if we can resolve a pointer to a type.
  ASSERT_OK_AND_ASSIGN(std::unique_ptr<TypeTree> type_tree_pointer,
                       type_resolver->ResolveTypeFromTypeName("A*"));
  ASSERT_TRUE(type_tree_pointer->Verify(/*verify_verbose=*/true));

  EXPECT_EQ(type_tree_pointer->Root()->GetTypeName(), "A*");
  EXPECT_EQ(type_tree_pointer->Root()->GetSizeBytes(), 8);
  EXPECT_EQ(type_tree_pointer->Root()->GetOffsetBytes(), 0);

  ASSERT_OK_AND_ASSIGN(std::unique_ptr<TypeTree> type_tree_reference,
                       type_resolver->ResolveTypeFromTypeName("A&"));
  ASSERT_TRUE(type_tree_reference->Verify(/*verify_verbose=*/true));
  EXPECT_EQ(type_tree_reference->Root()->GetTypeName(), "A&");
  EXPECT_EQ(type_tree_reference->Root()->GetSizeBytes(), 8);
  EXPECT_EQ(type_tree_reference->Root()->GetOffsetBytes(), 0);
}

// Check if the TypeTree inserts padding at the appropriate places.
TEST(TypeResolverTest, PaddingTypeTest) {
  const std::string dwarf_path =
      blaze_util::JoinPath(kTypeResolverTestPath, "padding_type.dwarf");
  const std::string linker_build_id = "ebe406c70a15578d";

  std::unique_ptr<BinaryFileRetriever> mock_retriever =
      BinaryFileRetriever::CreateMockRetriever({{linker_build_id, dwarf_path}});
  auto dwarf_metadata_fetcher = std::make_unique<DwarfMetadataFetcher>(
      std::move(mock_retriever), ::testing::TempDir());
  ASSERT_OK(
      dwarf_metadata_fetcher->FetchWithPath({{linker_build_id, dwarf_path}},
                                            /*force_update_cache=*/true));
  auto type_resolver =
      std::make_unique<DwarfTypeResolver>(std::move(dwarf_metadata_fetcher));

  ASSERT_OK_AND_ASSIGN(std::unique_ptr<TypeTree> type_tree_A,
                       type_resolver->ResolveTypeFromTypeName("A"));
  // Check padding between two fields.
  // class A {
  //  public:
  //   int x;
  //   /*padding*/
  //   long int y;
  // };
  EXPECT_TRUE(type_tree_A->Verify(/*verify_verbose=*/true));
  EXPECT_EQ(type_tree_A->Root()->GetTypeName(), "A");
  EXPECT_EQ(type_tree_A->Root()->GetSizeBytes(), 16);
  EXPECT_EQ(type_tree_A->Root()->GetOffsetBytes(), 0);
  ASSERT_EQ(type_tree_A->Root()->NumChildren(), 3);
  EXPECT_EQ(type_tree_A->Root()->GetChild(0)->GetTypeName(), "int");
  EXPECT_EQ(type_tree_A->Root()->GetChild(1)->GetTypeName(), "");
  EXPECT_EQ(type_tree_A->Root()->GetChild(2)->GetTypeName(), "long");
  EXPECT_EQ(type_tree_A->Root()->GetChild(0)->GetSizeBytes(), 4);
  EXPECT_EQ(type_tree_A->Root()->GetChild(1)->GetSizeBytes(), 4);
  EXPECT_EQ(type_tree_A->Root()->GetChild(2)->GetSizeBytes(), 8);
  EXPECT_EQ(type_tree_A->Root()->GetChild(0)->GetOffsetBytes(), 0);
  EXPECT_EQ(type_tree_A->Root()->GetChild(1)->GetOffsetBytes(), 4);
  EXPECT_EQ(type_tree_A->Root()->GetChild(2)->GetOffsetBytes(), 8);

  // Check padding at the end of all fields of a struct.
  // struct B {
  //  public:
  //   long int y;
  //   int x;
  // } __attribute__((packed));

  // struct C {
  //  public:
  //   B b;
  //   /*padding*/
  //   double x;
  // };
  ASSERT_OK_AND_ASSIGN(std::unique_ptr<TypeTree> type_tree_C,
                       type_resolver->ResolveTypeFromTypeName("C"));
  ASSERT_TRUE(type_tree_C->Verify(/*verify_verbose=*/true));
  EXPECT_EQ(type_tree_C->Root()->GetTypeName(), "C");
  EXPECT_EQ(type_tree_C->Root()->GetSizeBytes(), 24);
  EXPECT_EQ(type_tree_C->Root()->GetOffsetBytes(), 0);
  ASSERT_EQ(type_tree_C->Root()->NumChildren(), 3);
  EXPECT_EQ(type_tree_C->Root()->GetChild(0)->GetTypeName(), "B");
  EXPECT_TRUE(type_tree_C->Root()->GetChild(1)->IsPadding());
  EXPECT_EQ(type_tree_C->Root()->GetChild(1)->GetSizeBytes(), 4);
  EXPECT_EQ(type_tree_C->Root()->GetChild(1)->GetOffsetBytes(), 12);
  EXPECT_EQ(type_tree_C->Root()->GetChild(2)->GetSizeBytes(), 8);
  EXPECT_EQ(type_tree_C->Root()->GetChild(2)->GetOffsetBytes(), 16);
}

// Check if we can build a full type tree for STL map.
TEST(TypeResolverTest, StdMapTypeTest) {
  const std::string dwarf_path =
      blaze_util::JoinPath(kTypeResolverTestPath, "std_map_type.dwarf");
  const std::string linker_build_id = "25865f087139ad4e";

  std::unique_ptr<BinaryFileRetriever> mock_retriever =
      BinaryFileRetriever::CreateMockRetriever({{linker_build_id, dwarf_path}});
  auto dwarf_metadata_fetcher = std::make_unique<DwarfMetadataFetcher>(
      std::move(mock_retriever), ::testing::TempDir());
  ASSERT_OK(
      dwarf_metadata_fetcher->FetchWithPath({{linker_build_id, dwarf_path}},
                                            /*force_update_cache=*/true));
  auto type_resolver =
      std::make_unique<DwarfTypeResolver>(std::move(dwarf_metadata_fetcher));
  ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<TypeTree> type_tree,
      type_resolver->ResolveTypeFromTypeName(
          "std::_Rb_tree_node<std::pair<const unsigned long, A> >"));
  EXPECT_EQ(type_tree->Name(),
            "std::_Rb_tree_node<std::pair<const unsigned long, A> >");
  EXPECT_EQ(type_tree->Root()->GetTypeName(),
            "std::_Rb_tree_node<std::pair<const unsigned long, A> >");
  EXPECT_TRUE(type_tree->Verify(/*verify_verbose=*/true));
  EXPECT_EQ(type_tree->Root()->GetSizeBytes(), 56);
  EXPECT_EQ(type_tree->Root()->GetOffsetBytes(), 0);
  ASSERT_EQ(type_tree->Root()->NumChildren(), 2);
  EXPECT_EQ(type_tree->Root()->GetChild(0)->GetTypeName(),
            "std::_Rb_tree_node_base");
  EXPECT_EQ(type_tree->Root()->GetChild(0)->GetSizeBytes(), 32);
  ASSERT_EQ(type_tree->Root()->GetChild(0)->NumChildren(), 5);

  EXPECT_EQ(type_tree->Root()->GetChild(0)->GetChild(0)->GetTypeName(),
            "unsigned int");
  EXPECT_EQ(type_tree->Root()->GetChild(0)->GetChild(0)->GetName(), "_M_color");
  EXPECT_EQ(type_tree->Root()->GetChild(0)->GetChild(1)->GetTypeName(), "");
  EXPECT_EQ(type_tree->Root()->GetChild(0)->GetChild(1)->IsPadding(), true);
  EXPECT_EQ(type_tree->Root()->GetChild(0)->GetChild(1)->GetName(), "");
  EXPECT_EQ(type_tree->Root()->GetChild(0)->GetChild(2)->GetTypeName(),
            "std::_Rb_tree_node_base *");
  EXPECT_EQ(type_tree->Root()->GetChild(0)->GetChild(3)->GetTypeName(),
            "std::_Rb_tree_node_base *");
  EXPECT_EQ(type_tree->Root()->GetChild(0)->GetChild(4)->GetTypeName(),
            "std::_Rb_tree_node_base *");
  EXPECT_EQ(type_tree->Root()->GetChild(1)->GetTypeName(),
            "std::pair<const unsigned long, A>");
  ASSERT_EQ(type_tree->Root()->GetChild(1)->NumChildren(), 2);
  EXPECT_EQ(type_tree->Root()->GetChild(1)->GetChild(0)->GetTypeName(),
            "unsigned long");
  EXPECT_EQ(type_tree->Root()->GetChild(1)->GetChild(0)->GetName(), "first");

  EXPECT_EQ(type_tree->Root()->GetChild(1)->GetChild(0)->GetSizeBytes(), 8);
  EXPECT_EQ(type_tree->Root()->GetChild(1)->GetChild(1)->GetTypeName(), "A");
  EXPECT_EQ(type_tree->Root()->GetChild(1)->GetChild(1)->GetName(), "second");
  ASSERT_EQ(type_tree->Root()->GetChild(1)->GetChild(1)->NumChildren(), 2);
}

TEST(TypeResolverTest, UnionTypeTest) {
  const std::string dwarf_path =
      blaze_util::JoinPath(kTypeResolverTestPath, "union_type.dwarf");
  const std::string linker_build_id = "bac290aca8128893";

  std::unique_ptr<BinaryFileRetriever> mock_retriever =
      BinaryFileRetriever::CreateMockRetriever({{linker_build_id, dwarf_path}});
  auto dwarf_metadata_fetcher = std::make_unique<DwarfMetadataFetcher>(
      std::move(mock_retriever), ::testing::TempDir());
  ASSERT_OK(
      dwarf_metadata_fetcher->FetchWithPath({{linker_build_id, dwarf_path}},
                                            /*force_update_cache=*/true));
  auto type_resolver =
      std::make_unique<DwarfTypeResolver>(std::move(dwarf_metadata_fetcher));
  // struct B {
  //     int x;
  //     int y;
  // };
  // struct C {
  //     double x;
  // };
  // union A {
  //     B b;
  //     C c;
  // };
  // struct X {
  //     A a;
  // };
  ASSERT_OK_AND_ASSIGN(std::unique_ptr<TypeTree> type_tree,
                       type_resolver->ResolveTypeFromTypeName("X"));
  EXPECT_TRUE(type_tree->Verify(/*verify_verbose=*/true));
  EXPECT_EQ(type_tree->Root()->GetSizeBytes(), 8);
  EXPECT_EQ(type_tree->Root()->GetOffsetBytes(), 0);
  ASSERT_EQ(type_tree->Root()->NumChildren(), 1);
  EXPECT_EQ(type_tree->Root()->GetChild(0)->GetTypeName(), "A");
  EXPECT_EQ(type_tree->Root()->GetChild(0)->GetSizeBytes(), 8);
  EXPECT_EQ(type_tree->Root()->GetChild(0)->GetOffsetBytes(), 0);
  ASSERT_EQ(type_tree->Root()->GetChild(0)->NumChildren(), 2);
  EXPECT_TRUE(type_tree->Root()->GetChild(0)->IsUnion());
}

// Array type test. Checks if we can handle embedded arrays in structs. Right
// now, we do not further expand the TypeTree into the elements of the array.
TEST(TypeResolverTest, ArrayTypeTest) {
  const std::string dwarf_path =
      blaze_util::JoinPath(kTypeResolverTestPath, "array_type.dwarf");
  const std::string linker_build_id = "f1747dd609fe8c60";

  std::unique_ptr<BinaryFileRetriever> mock_retriever =
      BinaryFileRetriever::CreateMockRetriever({{linker_build_id, dwarf_path}});
  auto dwarf_metadata_fetcher = std::make_unique<DwarfMetadataFetcher>(
      std::move(mock_retriever), ::testing::TempDir());
  ASSERT_OK(
      dwarf_metadata_fetcher->FetchWithPath({{linker_build_id, dwarf_path}},
                                            /*force_update_cache=*/true));
  auto type_resolver =
      std::make_unique<DwarfTypeResolver>(std::move(dwarf_metadata_fetcher));

  // Basic array as field test.
  // struct A {
  //   long int x;
  //   int y[24];
  // };
  ASSERT_OK_AND_ASSIGN(std::unique_ptr<TypeTree> type_tree,
                       type_resolver->ResolveTypeFromTypeName("A"));
  ASSERT_TRUE(type_tree->Verify(/*verify_verbose=*/true));
  EXPECT_EQ(type_tree->Name(), "A");
  EXPECT_EQ(type_tree->Root()->GetTypeName(), "A");
  ASSERT_EQ(type_tree->Root()->NumChildren(), 2);
  EXPECT_EQ(type_tree->Root()->GetChild(0)->GetTypeName(), "long");
  EXPECT_EQ(type_tree->Root()->GetChild(1)->GetTypeName(), "int[24]");
  EXPECT_TRUE(type_tree->Root()->GetChild(1)->IsArrayType());
  // Array of struct test.
  // struct B {
  //   A a[12];
  // };
  ASSERT_OK_AND_ASSIGN(std::unique_ptr<TypeTree> type_tree_2,
                       type_resolver->ResolveTypeFromTypeName("B"));
  ASSERT_TRUE(type_tree_2->Verify(/*verify_verbose=*/true));
  EXPECT_TRUE(type_tree_2->Root()->IsRecordType());
  EXPECT_EQ(type_tree_2->Name(), "B");
  EXPECT_EQ(type_tree_2->Root()->GetTypeName(), "B");
  ASSERT_EQ(type_tree_2->Root()->NumChildren(), 1);
  EXPECT_EQ(type_tree_2->Root()->GetChild(0)->GetTypeName(), "A[12]");
  EXPECT_TRUE(type_tree_2->Root()->GetChild(0)->IsArrayType());

  // Array of arrays test.
  //  struct C {
  //    long int x;
  //    int y[24][24];
  //    A a[24][24];
  //  };
  ASSERT_OK_AND_ASSIGN(std::unique_ptr<TypeTree> type_tree_3,
                       type_resolver->ResolveTypeFromTypeName("C"));
  ASSERT_TRUE(type_tree_3->Verify(/*verify_verbose=*/true));
  EXPECT_TRUE(type_tree_3->Root()->IsRecordType());
  EXPECT_EQ(type_tree_3->Name(), "C");
  EXPECT_EQ(type_tree_3->Root()->GetTypeName(), "C");
  ASSERT_EQ(type_tree_3->Root()->NumChildren(), 3);
  EXPECT_EQ(type_tree_3->Root()->GetChild(0)->GetTypeName(), "long");
  EXPECT_EQ(type_tree_3->Root()->GetChild(1)->GetTypeName(), "int[24][24]");
  EXPECT_EQ(type_tree_3->Root()->GetChild(2)->GetTypeName(), "A[24][24]");
  ASSERT_EQ(type_tree_3->Root()->GetChild(1)->NumChildren(), 1);
  EXPECT_EQ(type_tree_3->Root()->GetChild(1)->GetChild(0)->GetTypeName(),
            "int[24]");
  ASSERT_EQ(type_tree_3->Root()->GetChild(2)->NumChildren(), 1);
  EXPECT_EQ(type_tree_3->Root()->GetChild(2)->GetChild(0)->GetTypeName(),
            "A[24]");

  // Array of pointers test.
  // struct D {
  //   A* a[12];
  // };
  ASSERT_OK_AND_ASSIGN(std::unique_ptr<TypeTree> type_tree_4,
                       type_resolver->ResolveTypeFromTypeName("D"));
  ASSERT_TRUE(type_tree_4->Verify(/*verify_verbose=*/true));
  EXPECT_TRUE(type_tree_4->Root()->IsRecordType());
  EXPECT_EQ(type_tree_4->Name(), "D");
  EXPECT_EQ(type_tree_4->Root()->GetTypeName(), "D");
  ASSERT_EQ(type_tree_4->Root()->NumChildren(), 1);
  EXPECT_EQ(type_tree_4->Root()->GetChild(0)->GetTypeName(), "A *[12]");
  ASSERT_EQ(type_tree_4->Root()->GetChild(0)->NumChildren(), 1);
  EXPECT_EQ(type_tree_4->Root()->GetChild(0)->GetChild(0)->GetTypeName(),
            "A *");

  // Array with padding after array test.
  // struct E {
  //   int x[3];
  //   double y;
  // };
  ASSERT_OK_AND_ASSIGN(std::unique_ptr<TypeTree> type_tree_5,
                       type_resolver->ResolveTypeFromTypeName("E"));
  ASSERT_TRUE(type_tree_5->Verify(/*verify_verbose=*/true));
  EXPECT_TRUE(type_tree_5->Root()->IsRecordType());
  EXPECT_EQ(type_tree_5->Name(), "E");
  EXPECT_EQ(type_tree_5->Root()->GetTypeName(), "E");
  ASSERT_EQ(type_tree_5->Root()->NumChildren(), 3);
  EXPECT_EQ(type_tree_5->Root()->GetChild(0)->GetTypeName(), "int[3]");
  EXPECT_TRUE(type_tree_5->Root()->GetChild(1)->IsPadding());
  EXPECT_EQ(type_tree_5->Root()->GetChild(2)->GetTypeName(), "double");
}

// Vector type test. Checks if we can resolve a common more complex STL type
// into a full TypeTree.
TEST(TypeResolverTest, VectorTypeTest) {
  const std::string dwarf_path =
      blaze_util::JoinPath(kTypeResolverTestPath, "vector_type.dwarf");
  const std::string linker_build_id = "9d9de85561da7496";

  std::unique_ptr<BinaryFileRetriever> mock_retriever =
      BinaryFileRetriever::CreateMockRetriever({{linker_build_id, dwarf_path}});
  auto dwarf_metadata_fetcher = std::make_unique<DwarfMetadataFetcher>(
      std::move(mock_retriever), ::testing::TempDir());
  ASSERT_OK(
      dwarf_metadata_fetcher->FetchWithPath({{linker_build_id, dwarf_path}},
                                            /*force_update_cache=*/true));
  auto type_resolver =
      std::make_unique<DwarfTypeResolver>(std::move(dwarf_metadata_fetcher));
  ASSERT_OK_AND_ASSIGN(std::unique_ptr<TypeTree> type_tree,
                       type_resolver->ResolveTypeFromTypeName(
                           "std::vector<double, std::allocator<double> >"));
  EXPECT_TRUE(type_tree->Verify(/*verify_verbose=*/true));
}

// Tests if we can create a TypeTree from an ObjectLayout.
TEST(TypeResolverTest, CreateFromObjectLayoutTest) {
  constexpr absl::string_view kObjectArrayLayout = R"pb(

    properties: {
      name: "xs",
      type_name: "unsigned char[16]",
      kind: FIELD,
      type_kind: ARRAY_TYPE,
      size_bits: 128,
      multiplicity: 1,
    },
    subobjects:
    [ {
      properties: {
        name: "x",
        type_name: "unsigned char",
        kind: ARRAY_ELEMENTS,
        type_kind: BUILTIN_TYPE,
        size_bits: 8,
        multiplicity: 16,
      },
    }],
  )pb";

  ObjectLayout object_layout_array;
  ASSERT_TRUE(google::protobuf::TextFormat::ParseFromString(
      kObjectArrayLayout, &object_layout_array));
  std::unique_ptr<TypeTree> type_tree_array =
      TypeTree::CreateTreeFromObjectLayout(object_layout_array, "xs");
  ASSERT_TRUE(type_tree_array->Verify(/*verify_verbose=*/true));
  EXPECT_EQ(type_tree_array->Name(), "xs");
  EXPECT_EQ(type_tree_array->Root()->GetName(), "xs");
  EXPECT_EQ(type_tree_array->Root()->GetTypeName(), "unsigned char[16]");
  EXPECT_TRUE(type_tree_array->Root()->IsArrayType());
  ASSERT_EQ(type_tree_array->Root()->NumChildren(), 1);
  EXPECT_EQ(type_tree_array->Root()->GetChild(0)->GetTypeName(),
            "unsigned char");
  EXPECT_EQ(type_tree_array->Root()->GetChild(0)->GetOffsetBytes(), 0);
  EXPECT_EQ(type_tree_array->Root()->GetChild(0)->GetSizeBits(), 8);
  EXPECT_EQ(type_tree_array->Root()->GetChild(0)->GetGlobalOffsetBytes(), 0);
  EXPECT_EQ(type_tree_array->Root()->GetChild(0)->GetTotalAccessCount(), 0);

  ObjectLayout object_layout_from_type_tree =
      TypeTree::CreateObjectLayoutFromTree(*type_tree_array);
  //   EXPECT_THAT(
  //       object_layout_from_type_tree,
  //       testing::proto::Partially(testing::EqualsProto(object_layout_array)));
}

// Tests that we can convert a full TypeTree into a flat representation of the
// tree in the form of a FieldAccessHistogram.
TEST(TypeResolverTest, FieldAccessHistogramTest) {
  const std::string dwarf_path =
      blaze_util::JoinPath(kTypeResolverTestPath, "std_map_type.dwarf");
  const std::string linker_build_id = "25865f087139ad4e";

  std::unique_ptr<BinaryFileRetriever> mock_retriever =
      BinaryFileRetriever::CreateMockRetriever({{linker_build_id, dwarf_path}});
  auto dwarf_metadata_fetcher = std::make_unique<DwarfMetadataFetcher>(
      std::move(mock_retriever), ::testing::TempDir());
  ASSERT_OK(
      dwarf_metadata_fetcher->FetchWithPath({{linker_build_id, dwarf_path}},
                                            /*force_update_cache=*/true));
  auto type_resolver =
      std::make_unique<DwarfTypeResolver>(std::move(dwarf_metadata_fetcher));
  ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<TypeTree> type_tree,
      type_resolver->ResolveTypeFromTypeName(
          "std::_Rb_tree_node<std::pair<const unsigned long, A> >"));
  ASSERT_TRUE(type_tree->Verify(/*verify_verbose=*/true));
  uint32_t histogram_size = 7;
  uint64_t histogram[7] = {1, 2, 3, 4, 5, 6, 7};

  ASSERT_OK(type_tree->RecordAccessHistogram(histogram, histogram_size));
  ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<FieldAccessHistogram> field_access_histogram,
      FieldAccessHistogram::Create(type_tree.get()));
  ASSERT_EQ(field_access_histogram->nodes_.size(), 8);
  // Check offsets.
  EXPECT_EQ(field_access_histogram->nodes_[0]->GetGlobalOffsetBytes(), 0);
  EXPECT_EQ(field_access_histogram->nodes_[1]->GetGlobalOffsetBytes(), 4);
  EXPECT_EQ(field_access_histogram->nodes_[2]->GetGlobalOffsetBytes(), 8);
  EXPECT_EQ(field_access_histogram->nodes_[3]->GetGlobalOffsetBytes(), 16);
  EXPECT_EQ(field_access_histogram->nodes_[4]->GetGlobalOffsetBytes(), 24);
  EXPECT_EQ(field_access_histogram->nodes_[5]->GetGlobalOffsetBytes(), 32);
  EXPECT_EQ(field_access_histogram->nodes_[6]->GetGlobalOffsetBytes(), 40);
  EXPECT_EQ(field_access_histogram->nodes_[7]->GetGlobalOffsetBytes(), 48);

  // Check sizes.
  EXPECT_EQ(field_access_histogram->nodes_[0]->GetSizeBytes(), 4);
  EXPECT_EQ(field_access_histogram->nodes_[1]->GetSizeBytes(), 4);
  EXPECT_EQ(field_access_histogram->nodes_[2]->GetSizeBytes(), 8);
  EXPECT_EQ(field_access_histogram->nodes_[3]->GetSizeBytes(), 8);
  EXPECT_EQ(field_access_histogram->nodes_[4]->GetSizeBytes(), 8);
  EXPECT_EQ(field_access_histogram->nodes_[5]->GetSizeBytes(), 8);
  EXPECT_EQ(field_access_histogram->nodes_[6]->GetSizeBytes(), 8);
  EXPECT_EQ(field_access_histogram->nodes_[7]->GetSizeBytes(), 8);

  // Check names.
  EXPECT_EQ(field_access_histogram->nodes_[0]->GetName(), "_M_color");
  EXPECT_EQ(field_access_histogram->nodes_[1]->GetName(), "");
  EXPECT_EQ(field_access_histogram->nodes_[2]->GetName(), "_M_parent");
  EXPECT_EQ(field_access_histogram->nodes_[3]->GetName(), "_M_left");
  EXPECT_EQ(field_access_histogram->nodes_[4]->GetName(), "_M_right");
  EXPECT_EQ(field_access_histogram->nodes_[5]->GetName(), "first");
  EXPECT_EQ(field_access_histogram->nodes_[6]->GetName(), "x");
  EXPECT_EQ(field_access_histogram->nodes_[7]->GetName(), "y");

  // Check types.
  EXPECT_EQ(field_access_histogram->nodes_[0]->GetTypeName(), "unsigned int");
  EXPECT_TRUE(field_access_histogram->nodes_[1]->IsPadding());
  EXPECT_TRUE(field_access_histogram->nodes_[2]->IsIndirectionType());
  EXPECT_TRUE(field_access_histogram->nodes_[3]->IsIndirectionType());
  EXPECT_TRUE(field_access_histogram->nodes_[4]->IsIndirectionType());
  EXPECT_EQ(field_access_histogram->nodes_[5]->GetTypeName(), "unsigned long");
  EXPECT_EQ(field_access_histogram->nodes_[6]->GetTypeName(), "double");
  EXPECT_EQ(field_access_histogram->nodes_[7]->GetTypeName(), "double");

  // Check access counts.
  EXPECT_EQ(field_access_histogram->nodes_[0]->GetTotalAccessCount(), 1);
  EXPECT_EQ(field_access_histogram->nodes_[1]->GetTotalAccessCount(), 1);
  EXPECT_EQ(field_access_histogram->nodes_[2]->GetTotalAccessCount(), 2);
  EXPECT_EQ(field_access_histogram->nodes_[3]->GetTotalAccessCount(), 3);
  EXPECT_EQ(field_access_histogram->nodes_[4]->GetTotalAccessCount(), 4);
  EXPECT_EQ(field_access_histogram->nodes_[5]->GetTotalAccessCount(), 5);
  EXPECT_EQ(field_access_histogram->nodes_[6]->GetTotalAccessCount(), 6);
  EXPECT_EQ(field_access_histogram->nodes_[7]->GetTotalAccessCount(), 7);

  // Check offset mapping.
  ASSERT_EQ(field_access_histogram->offset_to_idx_.size(), 8);
  EXPECT_EQ(field_access_histogram->offset_to_idx_.at(0), 0);
  EXPECT_EQ(field_access_histogram->offset_to_idx_.at(4), 1);
  EXPECT_EQ(field_access_histogram->offset_to_idx_.at(8), 2);
  EXPECT_EQ(field_access_histogram->offset_to_idx_.at(16), 3);
  EXPECT_EQ(field_access_histogram->offset_to_idx_.at(24), 4);
  EXPECT_EQ(field_access_histogram->offset_to_idx_.at(32), 5);
  EXPECT_EQ(field_access_histogram->offset_to_idx_.at(40), 6);
  EXPECT_EQ(field_access_histogram->offset_to_idx_.at(48), 7);
}

// Tests if we can correctly merge access counts from one TypeTree to another.
TEST(TypeResolverTest, MergeAccessCountsTest) {
  constexpr absl::string_view kStructA = R"pb(

    properties: {
      name: "A",
      type_name: "A",
      kind: FIELD,
      type_kind: RECORD_TYPE,
      size_bits: 128,
      multiplicity: 1,
      align_bits: 8
    },
    subobjects:
    [ {
      properties: {
        name: "x",
        type_name: "double",
        kind: FIELD,
        type_kind: BUILTIN_TYPE,
        offset_bits: 0,
        size_bits: 64,
        multiplicity: 1,
        align_bits: 8
      },
      summary: {}
    }
      , {
        properties: {
          name: "y",
          type_name: "double",
          kind: FIELD,
          type_kind: BUILTIN_TYPE,
          offset_bits: 64,
          size_bits: 64,
          multiplicity: 1,
          align_bits: 8
        },
        summary: {}
      }],
    summary: {}
  )pb";

  constexpr absl::string_view kStructB = R"pb(

    properties: {
      name: "B",
      type_name: "B",
      kind: FIELD,
      type_kind: BUILTIN_TYPE,
      size_bits: 8,
      multiplicity: 1,
      align_bits: 8
    },
    summary: {}
  )pb";

  ObjectLayout object_layout_A;
  ASSERT_TRUE(google::protobuf::TextFormat::ParseFromString(kStructA,
                                                            &object_layout_A));
  std::unique_ptr<TypeTree> type_tree =
      TypeTree::CreateTreeFromObjectLayout(object_layout_A, "A");
  ASSERT_TRUE(type_tree->Verify(/*verify_verbose=*/true));

  uint32_t histogram_size = 2;
  uint64_t histogram[] = {1, 2};
  ASSERT_OK(type_tree->RecordAccessHistogram(histogram, histogram_size));

  std::unique_ptr<TypeTree> type_tree_success =
      TypeTree::CreateTreeFromObjectLayout(object_layout_A, "A");
  uint64_t histogram_success[] = {3, 4};

  ASSERT_OK(
      type_tree->RecordAccessHistogram(histogram_success, histogram_size));

  ASSERT_OK(type_tree->MergeCounts(type_tree_success.get()));
  ASSERT_EQ(type_tree->Root()->NumChildren(), 2);
  EXPECT_EQ(type_tree->Root()->GetChild(0)->GetTotalAccessCount(), 4);
  EXPECT_EQ(type_tree->Root()->GetChild(1)->GetTotalAccessCount(), 6);

  ObjectLayout object_layout_fail;

  ASSERT_TRUE(google::protobuf::TextFormat::ParseFromString(
      kStructB, &object_layout_fail));
  std::unique_ptr<TypeTree> type_tree_fail =
      TypeTree::CreateTreeFromObjectLayout(object_layout_fail, "B");
  ASSERT_TRUE(type_tree->Verify(/*verify_verbose=*/true));

  EXPECT_NOT_OK(type_tree->MergeCounts(type_tree_fail.get()));
}

// Test that we can record access counts in a TypeTree.
TEST(TypeResolverTest, SimpleRecordAccessTest) {
  const std::string dwarf_path = blaze_util::JoinPath(
      kTypeResolverTestPath, "simple_record_access_type.dwarf");
  const std::string linker_build_id = "f5412ed20726e01a";

  std::unique_ptr<BinaryFileRetriever> mock_retriever =
      BinaryFileRetriever::CreateMockRetriever({{linker_build_id, dwarf_path}});
  auto dwarf_metadata_fetcher = std::make_unique<DwarfMetadataFetcher>(
      std::move(mock_retriever), ::testing::TempDir());
  ASSERT_OK(
      dwarf_metadata_fetcher->FetchWithPath({{linker_build_id, dwarf_path}},
                                            /*force_update_cache=*/true));
  auto type_resolver =
      std::make_unique<DwarfTypeResolver>(std::move(dwarf_metadata_fetcher));

  // Simple embedded struct access test.
  // class A {
  //  public:
  //   long int x;
  //   long int y;
  // };

  // class B {
  //  public:
  //   A a;
  // };
  ASSERT_OK_AND_ASSIGN(std::unique_ptr<TypeTree> type_tree_B,
                       type_resolver->ResolveTypeFromTypeName("B"));
  uint32_t histogram_size_B = 2;
  uint64_t histogram_B[] = {1, 2};

  // Make sure we fail with empty histogram granularity.
  ASSERT_NOT_OK(type_tree_B->RecordAccessHistogram(histogram_B, 0));

  ASSERT_OK(type_tree_B->RecordAccessHistogram(histogram_B, histogram_size_B));
  ASSERT_EQ(type_tree_B->Root()->NumChildren(), 1);
  EXPECT_EQ(type_tree_B->Root()->GetChild(0)->GetTotalAccessCount(), 3);
  ASSERT_EQ(type_tree_B->Root()->GetChild(0)->NumChildren(), 2);
  EXPECT_EQ(
      type_tree_B->Root()->GetChild(0)->GetChild(0)->GetTotalAccessCount(), 1);
  EXPECT_EQ(
      type_tree_B->Root()->GetChild(0)->GetChild(1)->GetTotalAccessCount(), 2);
  EXPECT_TRUE(type_tree_B->Verify(/*verify_verbose=*/true));

  // Tests to see if we can record accesses if the type is smaller than
  // histogram granularity.

  // class C { public:
  //   char c;
  // };
  ASSERT_OK_AND_ASSIGN(std::unique_ptr<TypeTree> type_tree_C,
                       type_resolver->ResolveTypeFromTypeName("C"));
  uint32_t histogram_size_C = 1;
  uint64_t histogram_C[] = {1};
  ASSERT_OK(type_tree_C->RecordAccessHistogram(histogram_C, histogram_size_C));
  ASSERT_EQ(type_tree_C->Root()->NumChildren(), 1);
  EXPECT_EQ(type_tree_C->Root()->GetChild(0)->GetTotalAccessCount(), 1);
  EXPECT_TRUE(type_tree_C->Verify(/*verify_verbose=*/true));

  // Tests to see if we can record accesses if the type is larger than
  // histogram granularity, but the alignments do not match.

  // class D {
  // public:
  //   int x;
  //   int y;
  //   int z;
  // } __attribute__((packed));
  ASSERT_OK_AND_ASSIGN(std::unique_ptr<TypeTree> type_tree_D,
                       type_resolver->ResolveTypeFromTypeName("D"));
  uint32_t histogram_size_D = 2;
  uint64_t histogram_D[] = {1, 2};
  ASSERT_OK(type_tree_D->RecordAccessHistogram(histogram_D, histogram_size_D));
  ASSERT_EQ(type_tree_D->Root()->NumChildren(), 3);
  EXPECT_EQ(type_tree_D->Root()->GetChild(0)->GetTotalAccessCount(), 1);
  EXPECT_EQ(type_tree_D->Root()->GetChild(1)->GetTotalAccessCount(), 1);
  EXPECT_EQ(type_tree_D->Root()->GetChild(2)->GetTotalAccessCount(), 2);
  ASSERT_TRUE(type_tree_D->Verify(/*verify_verbose=*/true));

  // Test if we can handle a bulk allocation with misalignment. For now we do
  // not consider counters to be shared and data can be inaccurate.
  uint32_t histogram_size_D_bulk = 4;
  uint64_t histogram_D_bulk[] = {1, 2, 3, 4};
  ASSERT_OK(type_tree_D->RecordAccessHistogram(histogram_D_bulk,
                                               histogram_size_D_bulk));

  EXPECT_EQ(type_tree_D->Root()->GetChild(0)->GetTotalAccessCount(), 5);
  EXPECT_EQ(type_tree_D->Root()->GetChild(1)->GetTotalAccessCount(), 5);
  EXPECT_EQ(type_tree_D->Root()->GetChild(2)->GetTotalAccessCount(), 8);

  ASSERT_TRUE(type_tree_D->Verify(/*verify_verbose=*/true));
}

// This test checks that we can record access counts of fields of an object
// inside an array embedded into another struct.
TEST(TypeResolverTest, ArrayAccessCountTest) {
  const std::string dwarf_path = blaze_util::JoinPath(
      kTypeResolverTestPath, "array_access_count_test.dwarf");
  const std::string linker_build_id = "158c92614fde7e6d";

  std::unique_ptr<BinaryFileRetriever> mock_retriever =
      BinaryFileRetriever::CreateMockRetriever({{linker_build_id, dwarf_path}});
  auto dwarf_metadata_fetcher = std::make_unique<DwarfMetadataFetcher>(
      std::move(mock_retriever), ::testing::TempDir());
  ASSERT_OK(
      dwarf_metadata_fetcher->FetchWithPath({{linker_build_id, dwarf_path}},
                                            /*force_update_cache=*/true));
  auto type_resolver =
      std::make_unique<DwarfTypeResolver>(std::move(dwarf_metadata_fetcher));
  ASSERT_OK_AND_ASSIGN(std::unique_ptr<TypeTree> type_tree_B,
                       type_resolver->ResolveTypeFromTypeName("B"));

  // Test access counts for a single layer of embedded type in array.
  // struct B {
  //   A a[4];
  // };
  uint32_t histogram_size_B = 8;
  uint64_t histogram_B[] = {0, 1, 2, 3, 4, 5, 6, 7};
  ASSERT_OK(type_tree_B->RecordAccessHistogram(histogram_B, histogram_size_B));
  ASSERT_TRUE(type_tree_B->Verify(/*verify_verbose=*/true));
  EXPECT_EQ(type_tree_B->Root()->GetTotalAccessCount(), 28);
  ASSERT_EQ(type_tree_B->Root()->NumChildren(), 1);
  EXPECT_EQ(type_tree_B->Root()->GetChild(0)->GetTotalAccessCount(), 28);
  ASSERT_EQ(type_tree_B->Root()->GetChild(0)->NumChildren(), 1);
  EXPECT_EQ(
      type_tree_B->Root()->GetChild(0)->GetChild(0)->GetTotalAccessCount(), 28);
  ASSERT_EQ(type_tree_B->Root()->GetChild(0)->GetChild(0)->NumChildren(), 2);
  EXPECT_EQ(type_tree_B->Root()
                ->GetChild(0)
                ->GetChild(0)
                ->GetChild(0)
                ->GetTotalAccessCount(),
            12);
  EXPECT_EQ(type_tree_B->Root()
                ->GetChild(0)
                ->GetChild(0)
                ->GetChild(1)
                ->GetTotalAccessCount(),
            16);

  // Test access counts for multiple layers of embedded arrays.
  // struct C {
  //   B b[4];
  // };
  ASSERT_OK_AND_ASSIGN(std::unique_ptr<TypeTree> type_tree_C,
                       type_resolver->ResolveTypeFromTypeName("C"));

  ASSERT_TRUE(type_tree_C->Verify(/*verify_verbose=*/true));
  uint32_t histogram_size_C = 32;
  uint64_t histogram_C[] = {0,  1,  2,  3,  4,  5,  6,  7,  8,  9,  10,
                            11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21,
                            22, 23, 24, 25, 26, 27, 28, 29, 30, 31};
  ASSERT_OK(type_tree_C->RecordAccessHistogram(histogram_C, histogram_size_C));
  ASSERT_TRUE(type_tree_C->Verify(/*verify_verbose=*/true));
  EXPECT_EQ(type_tree_C->Root()->GetTotalAccessCount(), 496);
  ASSERT_EQ(type_tree_C->Root()->NumChildren(), 1);
  EXPECT_EQ(type_tree_C->Root()->GetChild(0)->GetTotalAccessCount(), 496);
  ASSERT_EQ(type_tree_C->Root()->GetChild(0)->NumChildren(), 1);
  EXPECT_EQ(
      type_tree_C->Root()->GetChild(0)->GetChild(0)->GetTotalAccessCount(),
      496);
  ASSERT_EQ(type_tree_C->Root()->GetChild(0)->GetChild(0)->NumChildren(), 1);
  EXPECT_EQ(type_tree_C->Root()
                ->GetChild(0)
                ->GetChild(0)
                ->GetChild(0)
                ->GetTotalAccessCount(),
            496);
  ASSERT_EQ(
      type_tree_C->Root()->GetChild(0)->GetChild(0)->GetChild(0)->NumChildren(),
      1);
  EXPECT_EQ(type_tree_C->Root()
                ->GetChild(0)
                ->GetChild(0)
                ->GetChild(0)
                ->GetChild(0)
                ->GetTotalAccessCount(),
            496);
  ASSERT_EQ(type_tree_C->Root()
                ->GetChild(0)
                ->GetChild(0)
                ->GetChild(0)
                ->GetChild(0)
                ->NumChildren(),
            2);
  EXPECT_EQ(type_tree_C->Root()
                ->GetChild(0)
                ->GetChild(0)
                ->GetChild(0)
                ->GetChild(0)
                ->GetChild(0)
                ->GetTotalAccessCount(),
            240);
  EXPECT_EQ(type_tree_C->Root()
                ->GetChild(0)
                ->GetChild(0)
                ->GetChild(0)
                ->GetChild(0)
                ->GetChild(1)
                ->GetTotalAccessCount(),
            256);

  // Test accesses on types in array that are smaller than the histogram
  // granularity.
  //   struct D {
  //   int x[4];
  // };

  ASSERT_OK_AND_ASSIGN(std::unique_ptr<TypeTree> type_tree_D,
                       type_resolver->ResolveTypeFromTypeName("D"));

  uint32_t histogram_size_D = 2;
  uint64_t histogram_D[] = {1, 2};
  ASSERT_OK(type_tree_D->RecordAccessHistogram(histogram_D, histogram_size_D));
  ASSERT_TRUE(type_tree_D->Verify(/*verify_verbose=*/true));

  // Test access on array where starting offset of array is not zero.
  // struct E {
  //   double x;
  //   A a[4];
  // };
  ASSERT_OK_AND_ASSIGN(std::unique_ptr<TypeTree> type_tree_E,
                       type_resolver->ResolveTypeFromTypeName("E"));
  uint32_t histogram_size_E = 5;
  uint64_t histogram_E[] = {1, 2, 3, 4, 5};
  ASSERT_OK(type_tree_E->RecordAccessHistogram(histogram_E, histogram_size_E));
  ASSERT_TRUE(type_tree_E->Verify(/*verify_verbose=*/true));
  EXPECT_EQ(type_tree_E->Root()->GetTotalAccessCount(), 15);
  ASSERT_EQ(type_tree_E->Root()->NumChildren(), 2);
  EXPECT_EQ(type_tree_E->Root()->GetChild(0)->GetTotalAccessCount(), 1);
  EXPECT_EQ(type_tree_E->Root()->GetChild(1)->GetTotalAccessCount(), 14);
  ASSERT_EQ(type_tree_E->Root()->GetChild(1)->NumChildren(), 1);
  EXPECT_EQ(
      type_tree_E->Root()->GetChild(1)->GetChild(0)->GetTotalAccessCount(), 14);
  ASSERT_EQ(type_tree_E->Root()->GetChild(1)->GetChild(0)->NumChildren(), 2);
  EXPECT_EQ(type_tree_E->Root()
                ->GetChild(1)
                ->GetChild(0)
                ->GetChild(0)
                ->GetTotalAccessCount(),
            6);
  EXPECT_EQ(type_tree_E->Root()
                ->GetChild(1)
                ->GetChild(0)
                ->GetChild(1)
                ->GetTotalAccessCount(),
            8);
}

// This test checks if we can resolve unique pointers in containers.
TEST(TypeResolverTest, VectorUniquePointerTest) {
  const std::string dwarf_path = blaze_util::JoinPath(
      kTypeResolverTestPath, "vector_unique_pointer_type.dwarf");
  const std::string linker_build_id = "15e2e949dd6612ad";

  std::unique_ptr<BinaryFileRetriever> mock_retriever =
      BinaryFileRetriever::CreateMockRetriever({{linker_build_id, dwarf_path}});
  auto dwarf_metadata_fetcher = std::make_unique<DwarfMetadataFetcher>(
      std::move(mock_retriever), ::testing::TempDir(),
      /*read_subprograms=*/true);
  ASSERT_OK(
      dwarf_metadata_fetcher->FetchWithPath({{linker_build_id, dwarf_path}},
                                            /*force_update_cache=*/true));
  auto type_resolver =
      std::make_unique<DwarfTypeResolver>(std::move(dwarf_metadata_fetcher));

  // std::vector<std::unique_ptr<A>> As;
  // request size does not matter for this test.
  ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<TypeTree> type_tree,
      type_resolver->ResolveTypeFromResolutionStrategy(
          DwarfTypeResolver::ContainerResolutionStrategy(
              "std::vector",
              "_ZNSt15__new_allocatorISt10unique_ptrI1ASt14default_deleteIS1_"
              "EEE8allocateEmPKv",
              DwarfTypeResolver::ContainerResolutionStrategy::
                  kAllocatorAllocate),
          {DwarfMetadataFetcher::Frame(
              "_ZNSt15__new_allocatorISt10unique_ptrI1ASt14default_deleteIS1_"
              "EEE8allocateEmPKv",
              kDummyLineColNo, kDummyLineColNo)},
          /*request_size=*/-1));
  ASSERT_TRUE(type_tree->Verify(/*verify_verbose=*/true));
  EXPECT_EQ(type_tree->Name(), "std::unique_ptr<A, std::default_delete<A> >");
}

// This test checks if we can resolve function types in containers..
TEST(TypeResolverTest, VectorFunctionTypeTest) {
  const std::string dwarf_path =
      blaze_util::JoinPath(kTypeResolverTestPath, "vector_function_type.dwarf");
  const std::string linker_build_id = "fbdb062b430f6c94";

  std::unique_ptr<BinaryFileRetriever> mock_retriever =
      BinaryFileRetriever::CreateMockRetriever({{linker_build_id, dwarf_path}});
  auto dwarf_metadata_fetcher = std::make_unique<DwarfMetadataFetcher>(
      std::move(mock_retriever), ::testing::TempDir(),
      /*read_subprograms=*/true);
  ASSERT_OK(
      dwarf_metadata_fetcher->FetchWithPath({{linker_build_id, dwarf_path}},
                                            /*force_update_cache=*/true));
  auto type_resolver =
      std::make_unique<DwarfTypeResolver>(std::move(dwarf_metadata_fetcher));

  // std::vector<std::function<void (const A &, int)>> As;
  // request size does not matter for this test.
  ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<TypeTree> type_tree,
      type_resolver->ResolveTypeFromResolutionStrategy(
          DwarfTypeResolver::ContainerResolutionStrategy(
              "std::vector",
              "_ZNSt15__new_allocatorISt8functionIFvRK1AiEEE8allocateEmPKv",
              DwarfTypeResolver::ContainerResolutionStrategy::
                  kAllocatorAllocate),
          {DwarfMetadataFetcher::Frame(
              "_ZNSt15__new_allocatorISt8functionIFvRK1AiEEE8allocateEmPKv",
              kDummyLineColNo, kDummyLineColNo)},
          /*request_size=*/-1));
  ASSERT_TRUE(type_tree->Verify(/*verify_verbose=*/true));
  EXPECT_EQ(type_tree->Name(), "std::function<void (const A &, int)>");
}

// This test checks if we can resolve const pointers in containers.
TEST(TypeResolverTest, ConstPointerTest) {
  const std::string dwarf_path =
      blaze_util::JoinPath(kTypeResolverTestPath, "const_pointer_type.dwarf");
  const std::string linker_build_id = "51bded6ccf11062e";

  std::unique_ptr<BinaryFileRetriever> mock_retriever =
      BinaryFileRetriever::CreateMockRetriever({{linker_build_id, dwarf_path}});
  auto dwarf_metadata_fetcher = std::make_unique<DwarfMetadataFetcher>(
      std::move(mock_retriever), ::testing::TempDir(),
      /*read_subprograms=*/true);
  ASSERT_OK(
      dwarf_metadata_fetcher->FetchWithPath({{linker_build_id, dwarf_path}},
                                            /*force_update_cache=*/true));
  auto type_resolver =
      std::make_unique<DwarfTypeResolver>(std::move(dwarf_metadata_fetcher));

  // std::vector<const A *> As;
  // request size does not matter for this test.
  ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<TypeTree> type_tree,
      type_resolver->ResolveTypeFromResolutionStrategy(
          DwarfTypeResolver::ContainerResolutionStrategy(
              "std::vector", "_ZNSt15__new_allocatorIPK1AE8allocateEmPKv",
              DwarfTypeResolver::ContainerResolutionStrategy::
                  kAllocatorAllocate),
          {DwarfMetadataFetcher::Frame(
              "_ZNSt15__new_allocatorIPK1AE8allocateEmPKv", kDummyLineColNo,
              kDummyLineColNo)},
          /*request_size=*/-1));
  ASSERT_TRUE(type_tree->Verify(/*verify_verbose=*/true));
  EXPECT_EQ(type_tree->Name(), "A*");
}

TEST(TypeResolverTest, TypeTreeMergeTest) {
  constexpr absl::string_view kInnerObjectLayout = R"pb(
    properties: {
      name: "A",
      type_name: "A",
      kind: FIELD,
      type_kind: RECORD_TYPE,
      size_bits: 128,
      multiplicity: 1,
    },
    subobjects:
    [ {
      properties: {
        name: "x",
        type_name: "char",
        kind: ARRAY_ELEMENTS,
        type_kind: BUILTIN_TYPE,
        size_bits: 8,
        multiplicity: 16,
      },
    }],
  )pb";

  ObjectLayout inner_object_layout;
  ASSERT_TRUE(google::protobuf::TextFormat::ParseFromString(
      kInnerObjectLayout, &inner_object_layout));

  std::unique_ptr<devtools_crosstool_fdo_field_access::TypeTree> inner_tree =
      devtools_crosstool_fdo_field_access::TypeTree::CreateTreeFromObjectLayout(
          inner_object_layout, "A");

  constexpr absl::string_view kObjectArrayLayout = R"pb(
    properties: {
      name: "C",
      type_name: "C",
      kind: FIELD,
      type_kind: BUILTIN_TYPE,
      size_bits: 0,
      multiplicity: 1,
    },
    subobjects {
      properties: {
        name: "a",
        type_name: "A",
        kind: FIELD,
        type_kind: RECORD_TYPE,
        size_bits: 0,
        multiplicity: 1,
      }
    }
    subobjects {
      properties: {
        name: "b",
        type_name: "B",
        kind: FIELD,
        type_kind: BUILTIN_TYPE,
        size_bits: 8,
        multiplicity: 1,
      },
    },
  )pb";
  ObjectLayout outer_object_layout;
  ASSERT_TRUE(google::protobuf::TextFormat::ParseFromString(
      kObjectArrayLayout, &outer_object_layout));
  std::unique_ptr<devtools_crosstool_fdo_field_access::TypeTree> type_tree =
      devtools_crosstool_fdo_field_access::TypeTree::CreateTreeFromObjectLayout(
          outer_object_layout, "C");
  ASSERT_OK(type_tree->MergeTreeIntoThis(inner_tree.get()));
  ASSERT_TRUE(type_tree->Verify(/*verify_verbose=*/true));
  EXPECT_EQ(type_tree->Root()->GetFullSizeBits(), 136);
  EXPECT_EQ(type_tree->Root()->NumChildren(), 2);
  ASSERT_EQ(type_tree->Root()->GetChild(0)->NumChildren(), 1);
  EXPECT_EQ(type_tree->Root()->GetChild(0)->GetTypeName(), "A");
  EXPECT_EQ(type_tree->Root()->GetChild(0)->GetFullSizeBits(), 128);
  EXPECT_EQ(type_tree->Root()->GetChild(0)->GetChild(0)->NumChildren(), 0);
  ASSERT_EQ(type_tree->Root()->GetChild(0)->GetChild(0)->GetTypeName(), "char");
}

// This test checks if we can resolve simple union types.
TEST(TypeResolverTest, SimpleUnionTypeTest) {
  const std::string dwarf_path =
      blaze_util::JoinPath(kTypeResolverTestPath, "simple_union_type.dwarf");
  const std::string linker_build_id = "237d613e3cc628de";
  const std::string type_name = "SimpleUnion";

  std::unique_ptr<BinaryFileRetriever> mock_retriever =
      BinaryFileRetriever::CreateMockRetriever({{linker_build_id, dwarf_path}});
  auto dwarf_metadata_fetcher = std::make_unique<DwarfMetadataFetcher>(
      std::move(mock_retriever), ::testing::TempDir(),
      /*read_subprograms=*/true);
  ASSERT_OK(
      dwarf_metadata_fetcher->FetchWithPath({{linker_build_id, dwarf_path}},
                                            /*force_update_cache=*/true));
  auto type_resolver =
      std::make_unique<DwarfTypeResolver>(std::move(dwarf_metadata_fetcher));
  ASSERT_OK_AND_ASSIGN(std::unique_ptr<TypeTree> type_tree,
                       type_resolver->ResolveTypeFromTypeName(type_name));
  // struct SimpleUnion {
  //   union A {
  //     int x;
  //     double y;
  //   };
  //   A a;
  // };
  type_tree->Verify(/*verify_verbose=*/true);
  EXPECT_EQ(type_tree->Root()->GetName(), "SimpleUnion");
  ASSERT_EQ(type_tree->Root()->NumChildren(), 1);
  EXPECT_EQ(type_tree->Root()->GetChild(0)->GetName(), "a");
  EXPECT_TRUE(type_tree->Root()->GetChild(0)->IsUnion());
  EXPECT_EQ(type_tree->Root()->GetChild(0)->GetTypeName(), "SimpleUnion::A");
  ASSERT_EQ(type_tree->Root()->GetChild(0)->NumChildren(), 2);
  EXPECT_EQ(type_tree->Root()->GetChild(0)->GetChild(0)->GetName(), "x");
  EXPECT_EQ(type_tree->Root()->GetChild(0)->GetChild(0)->GetTypeName(), "int");
  EXPECT_EQ(type_tree->Root()->GetChild(0)->GetChild(1)->GetName(), "y");
  EXPECT_EQ(type_tree->Root()->GetChild(0)->GetChild(1)->GetTypeName(),
            "float");
}

// This test checks if we can resolve anonymous union types.
TEST(TypeResolverTest, AnonymousUnionTypeTest) {
  const std::string dwarf_path =
      blaze_util::JoinPath(kTypeResolverTestPath, "anonymous_union_type.dwarf");
  const std::string linker_build_id = "b9994af308c2237f";
  const std::string type_name = "AnonymousUnion";

  std::unique_ptr<BinaryFileRetriever> mock_retriever =
      BinaryFileRetriever::CreateMockRetriever({{linker_build_id, dwarf_path}});
  auto dwarf_metadata_fetcher = std::make_unique<DwarfMetadataFetcher>(
      std::move(mock_retriever), ::testing::TempDir(),
      /*read_subprograms=*/true);
  ASSERT_OK(
      dwarf_metadata_fetcher->FetchWithPath({{linker_build_id, dwarf_path}},
                                            /*force_update_cache=*/true));
  auto type_resolver =
      std::make_unique<DwarfTypeResolver>(std::move(dwarf_metadata_fetcher));
  ASSERT_OK_AND_ASSIGN(std::unique_ptr<TypeTree> type_tree,
                       type_resolver->ResolveTypeFromTypeName(type_name));
  ASSERT_TRUE(type_tree->Verify(/*verify_verbose=*/true));
  //   union {
  //     int x;
  //     double y;
  //   };
  //   union {
  //     Intern1 i1;
  //     Intern2 i2;
  //   };
  //   union {
  //     Intern1 i3;
  //   };
  // };
  EXPECT_EQ(type_tree->Root()->GetName(), "AnonymousUnion");
  ASSERT_EQ(type_tree->Root()->NumChildren(), 3);
  EXPECT_TRUE(type_tree->Root()->GetChild(0)->IsUnion());
  ASSERT_EQ(type_tree->Root()->GetChild(0)->NumChildren(), 2);
  EXPECT_EQ(type_tree->Root()->GetChild(0)->GetChild(0)->GetName(), "x");
  EXPECT_EQ(type_tree->Root()->GetChild(0)->GetChild(0)->GetTypeName(), "int");
  EXPECT_EQ(type_tree->Root()->GetChild(0)->GetChild(1)->GetName(), "y");
  EXPECT_EQ(type_tree->Root()->GetChild(0)->GetChild(1)->GetTypeName(),
            "double");
  EXPECT_TRUE(type_tree->Root()->GetChild(1)->IsUnion());
  ASSERT_EQ(type_tree->Root()->GetChild(1)->NumChildren(), 2);
  EXPECT_EQ(type_tree->Root()->GetChild(1)->GetChild(0)->GetName(), "i1");
  EXPECT_EQ(type_tree->Root()->GetChild(1)->GetChild(0)->GetTypeName(),
            "Intern1");
  EXPECT_EQ(type_tree->Root()->GetChild(1)->GetChild(1)->GetName(), "i2");
  EXPECT_EQ(type_tree->Root()->GetChild(1)->GetChild(1)->GetTypeName(),
            "Intern2");
  EXPECT_TRUE(type_tree->Root()->GetChild(2)->IsUnion());
  ASSERT_EQ(type_tree->Root()->GetChild(2)->NumChildren(), 1);
  EXPECT_EQ(type_tree->Root()->GetChild(2)->GetChild(0)->GetName(), "i3");
  EXPECT_EQ(type_tree->Root()->GetChild(2)->GetChild(0)->GetTypeName(),
            "Intern1");
}

// This test checks if we can resolve types with std::optional fields.
TEST(TypeResolverTest, StdOptionalTypeTest) {
  const std::string dwarf_path =
      blaze_util::JoinPath(kTypeResolverTestPath, "std_optional_type.dwarf");
  const std::string linker_build_id = "0d4667e5e9c4f29f";
  const std::string type_name = "B";

  std::unique_ptr<BinaryFileRetriever> mock_retriever =
      BinaryFileRetriever::CreateMockRetriever({{linker_build_id, dwarf_path}});
  auto dwarf_metadata_fetcher = std::make_unique<DwarfMetadataFetcher>(
      std::move(mock_retriever), ::testing::TempDir(),
      /*read_subprograms=*/true);
  ASSERT_OK(
      dwarf_metadata_fetcher->FetchWithPath({{linker_build_id, dwarf_path}},
                                            /*force_update_cache=*/true));
  auto type_resolver =
      std::make_unique<DwarfTypeResolver>(std::move(dwarf_metadata_fetcher));
  ASSERT_OK_AND_ASSIGN(std::unique_ptr<TypeTree> type_tree,
                       type_resolver->ResolveTypeFromTypeName(type_name));
  // struct A {
  //   int x;
  //   int y;
  // };
  // struct B {
  //   std::optional<A> a;
  // };
  ASSERT_TRUE(type_tree->Verify(/*verify_verbose=*/true));
  EXPECT_EQ(type_tree->Root()->GetName(), "B");
  EXPECT_EQ(type_tree->Root()->GetSizeBytes(), 12);
  ASSERT_EQ(type_tree->Root()->NumChildren(), 1);
  EXPECT_EQ(type_tree->Root()->GetChild(0)->GetName(), "a");
  EXPECT_EQ(type_tree->Root()->GetChild(0)->GetTypeName(), "std::optional<A>");
  ASSERT_EQ(type_tree->Root()->GetChild(0)->NumChildren(), 1);
  EXPECT_EQ(type_tree->Root()->GetChild(0)->GetChild(0)->GetName(),
            "_Optional_base<A, true, true>");

  EXPECT_EQ(type_tree->Root()->GetChild(0)->GetChild(0)->GetTypeName(),
            "std::_Optional_base<A, true, true>");
  ASSERT_EQ(type_tree->Root()->GetChild(0)->GetChild(0)->NumChildren(), 1);
  EXPECT_EQ(type_tree->Root()->GetChild(0)->GetChild(0)->GetChild(0)->GetName(),
            "_M_payload");
  EXPECT_EQ(
      type_tree->Root()->GetChild(0)->GetChild(0)->GetChild(0)->GetTypeName(),
      "std::_Optional_payload<A, true, true, true>");
  ASSERT_EQ(
      type_tree->Root()->GetChild(0)->GetChild(0)->GetChild(0)->NumChildren(),
      1);
  EXPECT_EQ(type_tree->Root()
                ->GetChild(0)
                ->GetChild(0)
                ->GetChild(0)
                ->GetChild(0)
                ->GetTypeName(),
            "std::_Optional_payload_base<A>");
  ASSERT_EQ(type_tree->Root()
                ->GetChild(0)
                ->GetChild(0)
                ->GetChild(0)
                ->GetChild(0)
                ->NumChildren(),
            3);
  EXPECT_EQ(type_tree->Root()
                ->GetChild(0)
                ->GetChild(0)
                ->GetChild(0)
                ->GetChild(0)
                ->GetChild(0)
                ->GetTypeName(),
            "std::_Optional_payload_base<A>::_Storage<A, true>");
  EXPECT_EQ(type_tree->Root()
                ->GetChild(0)
                ->GetChild(0)
                ->GetChild(0)
                ->GetChild(0)
                ->GetChild(1)
                ->GetTypeName(),
            "bool");
  EXPECT_TRUE(type_tree->Root()
                  ->GetChild(0)
                  ->GetChild(0)
                  ->GetChild(0)
                  ->GetChild(0)
                  ->GetChild(2)
                  ->IsPadding());
  // Payload.
  ASSERT_EQ(type_tree->Root()
                ->GetChild(0)
                ->GetChild(0)
                ->GetChild(0)
                ->GetChild(0)
                ->GetChild(0)
                ->NumChildren(),
            2);
  EXPECT_EQ(type_tree->Root()
                ->GetChild(0)
                ->GetChild(0)
                ->GetChild(0)
                ->GetChild(0)
                ->GetChild(0)
                ->GetChild(1)
                ->GetTypeName(),
            "A");
  ASSERT_EQ(type_tree->Root()
                ->GetChild(0)
                ->GetChild(0)
                ->GetChild(0)
                ->GetChild(0)
                ->GetChild(0)
                ->GetChild(1)
                ->NumChildren(),
            2);
}

TEST(TypeResolverTest, SimpleProtoTest) {
  const std::string dwarf_path =
      blaze_util::JoinPath(kTypeResolverTestPath, "proto_simple.dwp");
  const std::string linker_build_id = "";
  const std::string type_name = "testdata::Record";

  std::unique_ptr<BinaryFileRetriever> mock_retriever =
      BinaryFileRetriever::CreateMockRetriever({{linker_build_id, dwarf_path}});
  auto dwarf_metadata_fetcher = std::make_unique<DwarfMetadataFetcher>(
      std::move(mock_retriever), ::testing::TempDir(),
      /*read_subprograms=*/true);
  ASSERT_OK(dwarf_metadata_fetcher->FetchDWPWithPath(
      {{.build_id = linker_build_id, .path = dwarf_path}},
      /*force_update_cache=*/true));
  auto type_resolver =
      std::make_unique<DwarfTypeResolver>(std::move(dwarf_metadata_fetcher));
  ASSERT_OK_AND_ASSIGN(std::unique_ptr<TypeTree> type_tree,
                       type_resolver->ResolveTypeFromTypeName(type_name));
  // Simple Record message.
  // message Record {
  //   int32 id = 1;
  //   string name = 2;
  // }
  ASSERT_TRUE(type_tree->Verify(/*verify_verbose=*/true));
  EXPECT_EQ(type_tree->Root()->GetTypeName(), "testdata::Record");
  EXPECT_EQ(type_tree->Root()->GetSizeBytes(), 32);
  ASSERT_EQ(type_tree->Root()->NumChildren(), 2);
  EXPECT_EQ(type_tree->Root()->GetChild(0)->GetTypeName(), "google::protobuf::Message");
  EXPECT_EQ(type_tree->Root()->GetChild(0)->GetSizeBytes(), 16);
  EXPECT_EQ(type_tree->Root()->GetChild(1)->GetTypeName().substr(0, 16),
            "testdata::Record");
  ASSERT_EQ(type_tree->Root()->GetChild(1)->NumChildren(), 1);
  ASSERT_EQ(type_tree->Root()->GetChild(1)->GetChild(0)->NumChildren(), 3);
  EXPECT_EQ(
      type_tree->Root()->GetChild(1)->GetChild(0)->GetChild(0)->GetTypeName(),
      "google::protobuf::internal::ArenaStringPtr");
  EXPECT_EQ(
      type_tree->Root()->GetChild(1)->GetChild(0)->GetChild(1)->GetTypeName(),
      "int");
  EXPECT_EQ(
      type_tree->Root()->GetChild(1)->GetChild(0)->GetChild(2)->GetTypeName(),
      "google::protobuf::internal::CachedSize");
}

TEST(TypeResolverTest, ComplexProtoTest) {
  const std::string dwarf_path =
      blaze_util::JoinPath(kTypeResolverTestPath, "proto_complex.dwp");
  const std::string linker_build_id = "";
  const std::string type_name = "testdata::SearchResponse";

  std::unique_ptr<BinaryFileRetriever> mock_retriever =
      BinaryFileRetriever::CreateMockRetriever({{linker_build_id, dwarf_path}});
  auto dwarf_metadata_fetcher = std::make_unique<DwarfMetadataFetcher>(
      std::move(mock_retriever), ::testing::TempDir(),
      /*read_subprograms=*/true);
  ASSERT_OK(dwarf_metadata_fetcher->FetchDWPWithPath(
      {{.build_id = linker_build_id, .path = dwarf_path}},
      /*force_update_cache=*/true));
  auto type_resolver =
      std::make_unique<DwarfTypeResolver>(std::move(dwarf_metadata_fetcher));
  ASSERT_OK_AND_ASSIGN(std::unique_ptr<TypeTree> type_tree,
                       type_resolver->ResolveTypeFromTypeName(type_name));
  ASSERT_TRUE(type_tree->Verify(/*verify_verbose=*/true));
  EXPECT_EQ(type_tree->Root()->GetTypeName(), "testdata::SearchResponse");
  EXPECT_EQ(type_tree->Root()->GetSizeBytes(), 72);
  ASSERT_EQ(type_tree->Root()->NumChildren(), 2);
  EXPECT_EQ(type_tree->Root()->GetChild(0)->GetTypeName(), "google::protobuf::Message");
  EXPECT_EQ(type_tree->Root()->GetChild(0)->GetSizeBytes(), 16);
  ASSERT_EQ(type_tree->Root()->GetChild(1)->NumChildren(), 1);
  ASSERT_EQ(type_tree->Root()->GetChild(1)->GetChild(0)->NumChildren(), 3);
  EXPECT_EQ(
      type_tree->Root()->GetChild(1)->GetChild(0)->GetChild(0)->GetTypeName(),
      "google::protobuf::internal::MapField<testdata::SearchResponse_ResultsByPageEntry_"
      "DoNotUse, int, testdata::Result, "
      "(google::protobuf::internal::WireFormatLite::FieldType)5, "
      "(google::protobuf::internal::WireFormatLite::FieldType)11>");
  EXPECT_EQ(
      type_tree->Root()->GetChild(1)->GetChild(0)->GetChild(0)->GetSizeBytes(),
      48);
  EXPECT_EQ(
      type_tree->Root()->GetChild(1)->GetChild(0)->GetChild(1)->GetTypeName(),
      "google::protobuf::internal::CachedSize");
  EXPECT_EQ(
      type_tree->Root()->GetChild(1)->GetChild(0)->GetChild(1)->GetSizeBytes(),
      4);
}

TEST(TypeResolverTest, UnwrapAndCleanTypeNameTest) {
  EXPECT_EQ(DwarfTypeResolver::UnwrapAndCleanTypeName("std::allocator<int>"),
            "int");
  EXPECT_EQ(DwarfTypeResolver::UnwrapAndCleanTypeName(
                "PolymorphicAllocator<int, false>"),
            "int");
  EXPECT_EQ(DwarfTypeResolver::UnwrapAndCleanTypeName(
                "muppet::instant::PolymorphicAllocator<std::__u::pair<const "
                "int, muppet::instant::ResourcedSharedString *>, false>"),
            "std::__u::pair<const "
            "int, muppet::instant::ResourcedSharedString *>");
}

}  // namespace
}  // namespace devtools_crosstool_fdo_field_access
