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

#include "histogram_builder.h"

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "dwarf_metadata_fetcher.h"
#include "gtest/gtest.h"
#include "llvm/include/llvm/ProfileData/MemProf.h"
#include "src/object_layout.pb.h"
#include "src/main/cpp/util/path.h"
#include "status_macros.h"
#include "test_status_macros.h"
#include "type_tree.h"

namespace devtools_crosstool_fdo_field_access {

namespace {
constexpr const char* kHistogramBuilderTestPath = "src/testdata";

constexpr uint64_t kDummyFrameGUID = 0xDEADBEEF;

llvm::memprof::Frame CreateFrame(std::string symbol_name, uint64_t line_offset,
                                 uint64_t col_offset) {
  llvm::memprof::Frame frame(kDummyFrameGUID, line_offset, col_offset, false);
  frame.SymbolName = std::make_unique<std::string>(symbol_name);
  return frame;
}

const TypeTree* GetTypeTreeForContainer(const TypeTreeStore* type_tree_store,
                                        absl::string_view container_type_name,
                                        absl::string_view type_name) {
  for (const auto& [unused, type_tree] :
       type_tree_store->callstack_to_type_tree_) {
    if (type_tree->Root()->GetTypeName() == type_name &&
        type_tree->ContainerName() == container_type_name) {
      return type_tree.get();
    }
  }
  return nullptr;
}

int GetNumTypeTreesForContainer(const TypeTreeStore* type_tree_store,
                                absl::string_view container_type_name) {
  int num_type_trees = 0;
  for (const auto& [unused, type_tree] :
       type_tree_store->callstack_to_type_tree_) {
    if (type_tree->ContainerName() == container_type_name) {
      ++num_type_trees;
    }
  }
  return num_type_trees;
}

bool TypeTreeHasNodeWithTypeName(const TypeTree::Node* node,
                                 absl::string_view type_name) {
  if (node->GetTypeName() == type_name) {
    return true;
  }

  bool res = false;
  for (int i = 0; i < node->NumChildren(); ++i) {
    auto child = node->GetChild(i);
    res |= TypeTreeHasNodeWithTypeName(child, type_name);
  }
  return res;
}

bool TypeTreeHasNodeWithTypeName(const TypeTree* type_tree,
                                 absl::string_view type_name) {
  return TypeTreeHasNodeWithTypeName(type_tree->Root(), type_name);
}

bool TypeTreeStoreHasTypeTreeForContainer(const TypeTreeStore* type_tree_store,
                                          absl::string_view container_type_name,
                                          absl::string_view type_name) {
  return GetTypeTreeForContainer(type_tree_store, container_type_name,
                                 type_name) != nullptr;
}

bool TypeTreeStoreTypeTreeForContainerHasNodeWithTypeName(
    const TypeTreeStore* type_tree_store, absl::string_view container_type_name,
    absl::string_view type_name, absl::string_view node_name) {
  auto type_tree =
      GetTypeTreeForContainer(type_tree_store, container_type_name, type_name);
  if (type_tree == nullptr) {
    return false;
  }
  return TypeTreeHasNodeWithTypeName(type_tree, node_name);
}

static bool IsCallStackInVector(
    const TypeTreeStore::CallStack& callstack,
    absl::Span<const TypeTreeStore::CallStack> callstacks) {
  for (const auto& callstack_in_vector : callstacks) {
    if (callstack_in_vector == callstack) {
      return true;
    }
  }
  return false;
}

TEST(HistogramBuilderTest, TypeTreeStoreTest) {
  TypeTreeStore type_tree_store = TypeTreeStore();

  ObjectLayout object_layout;
  object_layout.mutable_properties()->set_name("A");
  object_layout.mutable_properties()->set_type_name("A");
  object_layout.mutable_properties()->set_type_kind(
      ObjectLayout::Properties::BUILTIN_TYPE);
  object_layout.mutable_properties()->set_size_bits(8 * 8);
  object_layout.mutable_properties()->set_offset_bits(0);

  std::unique_ptr<TypeTree> type_tree =
      TypeTree::CreateTreeFromObjectLayout(object_layout, "A");

  ASSERT_TRUE(type_tree->Verify(/*verify_verbose=*/true));

  llvm::memprof::Frame frame1 = CreateFrame("foo", 1, 2);
  llvm::memprof::Frame frame2 = CreateFrame("bar", 3, 4);
  llvm::memprof::Frame frame3 = CreateFrame("baz", 5, 6);
  llvm::memprof::Frame frame4 = CreateFrame("qux", 6, 7);
  std::vector<llvm::memprof::Frame> callstack = {frame1, frame2, frame3};

  // Check that the callstack is converted correctly.
  std::vector<DwarfMetadataFetcher::Frame> dwarf_callstack =
      TypeTreeStore::ConvertCallStack(callstack);

  ASSERT_EQ(dwarf_callstack.size(), 3);
  EXPECT_EQ(dwarf_callstack.at(0).function_name, "foo");
  EXPECT_EQ(dwarf_callstack.at(0).line_offset, 1);
  EXPECT_EQ(dwarf_callstack.at(0).column, 2);
  EXPECT_EQ(dwarf_callstack.at(1).function_name, "bar");
  EXPECT_EQ(dwarf_callstack.at(1).line_offset, 3);
  EXPECT_EQ(dwarf_callstack.at(1).column, 4);
  EXPECT_EQ(dwarf_callstack.at(2).function_name, "baz");
  EXPECT_EQ(dwarf_callstack.at(2).line_offset, 5);
  EXPECT_EQ(dwarf_callstack.at(2).column, 6);

  TypeTreeStore trie = TypeTreeStore();
  ASSERT_OK_AND_ASSIGN(const TypeTree* const_type_tree,
                       trie.InsertAndGet(callstack, std::move(type_tree)));
  EXPECT_EQ(const_type_tree->Name(), "A");
  EXPECT_EQ(const_type_tree->Root()->GetTypeName(), "A");
  EXPECT_EQ(const_type_tree->Root()->GetSizeBytes(), 8);

  ObjectLayout object_layout_2;
  object_layout.mutable_properties()->set_name("B");
  object_layout.mutable_properties()->set_type_name("B");
  object_layout.mutable_properties()->set_type_kind(
      ObjectLayout::Properties::BUILTIN_TYPE);
  object_layout.mutable_properties()->set_size_bits(8 * 8);
  object_layout.mutable_properties()->set_offset_bits(0);

  std::unique_ptr<TypeTree> type_tree_2 =
      TypeTree::CreateTreeFromObjectLayout(object_layout_2, "B");

  // Make sure we can't insert a different type tree for the same callstack.
  EXPECT_NOT_OK(trie.InsertAndGet(callstack, std::move(type_tree_2)));

  std::unique_ptr<TypeTree> type_tree_3 =
      TypeTree::CreateTreeFromObjectLayout(object_layout, "A");
  std::vector<llvm::memprof::Frame> callstack_2 = {frame1, frame2, frame4};

  ASSERT_OK(trie.Insert(callstack_2, std::move(type_tree_3)));

  // Check that we can get the callstacks for a given type name.
  std::vector<TypeTreeStore::CallStack> callstacks =
      trie.GetCallStacksForTypeName("A");
  ASSERT_EQ(callstacks.size(), 2);

  ASSERT_EQ(callstacks.at(0).size(), 3);
  ASSERT_EQ(callstacks.at(1).size(), 3);
  EXPECT_TRUE(IsCallStackInVector(TypeTreeStore::ConvertCallStack(callstack),
                                  callstacks));
  EXPECT_TRUE(IsCallStackInVector(TypeTreeStore::ConvertCallStack(callstack_2),
                                  callstacks));

  // Check that we get a not found error for a callstack that doesn't exist.
  std::vector<llvm::memprof::Frame> not_found_callstack = {frame1, frame3,
                                                           frame4};
  ASSERT_NOT_OK(trie.GetTypeTree(not_found_callstack));

  ASSERT_OK_AND_ASSIGN(
      std::shared_ptr<TypeTree> type_tree_a,
      trie.GetTypeTree(TypeTreeStore::ConvertCallStack(callstack)));
  EXPECT_TRUE(type_tree_a->Verify(/*verify_verbose=*/true));
  EXPECT_EQ(type_tree_a->Name(), "A");
  EXPECT_EQ(type_tree_a->Root()->GetTypeName(), "A");
  EXPECT_EQ(type_tree_a->Root()->GetSizeBytes(), 8);
}

// This test checks that the histogram builder can correctly build a histogram
// for all the supported STL containers.
TEST(HistogramBuilderTest, SupportedContainersTest) {
  const std::string exe_path = blaze_util::JoinPath(
      kHistogramBuilderTestPath, "supported_stl_containers.exe");
  const std::string profile_path = blaze_util::JoinPath(
      kHistogramBuilderTestPath, "supported_stl_containers.memprofraw");

  ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<AbstractHistogramBuilder> histogram_builder,
      LocalHistogramBuilder::Create(
          profile_path, exe_path, exe_path, /*type_prefix_filter=*/{},
          /*callstack_filter=*/{}, /*only_records=*/false,
          /*verify_verbose=*/true, /*dump_unresolved_callstacks=*/true));
  ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<HistogramBuilderResults> histogram_builder_results,
      histogram_builder->BuildHistogram());

  const TypeTreeStore* type_tree_store =
      histogram_builder_results->type_tree_store.get();

  EXPECT_TRUE(TypeTreeStoreHasTypeTreeForContainer(type_tree_store,
                                                   "std::_Vector_base", "A"));
  EXPECT_TRUE(TypeTreeStoreTypeTreeForContainerHasNodeWithTypeName(
      type_tree_store, "std::_Vector_base", "A", "A"));

  EXPECT_TRUE(TypeTreeStoreHasTypeTreeForContainer(type_tree_store,
                                                   "std::_Deque_base", "A"));
  EXPECT_TRUE(TypeTreeStoreTypeTreeForContainerHasNodeWithTypeName(
      type_tree_store, "std::_Deque_base", "A", "A"));
  EXPECT_TRUE(TypeTreeStoreHasTypeTreeForContainer(
      type_tree_store, "std::_Rb_tree",
      "std::_Rb_tree_node<std::pair<const A, A> >"));
  EXPECT_TRUE(TypeTreeStoreTypeTreeForContainerHasNodeWithTypeName(
      type_tree_store, "std::_Rb_tree",
      "std::_Rb_tree_node<std::pair<const A, A> >", "A"));
  EXPECT_TRUE(TypeTreeStoreHasTypeTreeForContainer(
      type_tree_store, "std::_Rb_tree", "std::_Rb_tree_node<A>"));
  EXPECT_TRUE(TypeTreeStoreTypeTreeForContainerHasNodeWithTypeName(
      type_tree_store, "std::_Rb_tree", "std::_Rb_tree_node<A>", "A"));
  EXPECT_TRUE(TypeTreeStoreHasTypeTreeForContainer(
      type_tree_store, "std::_Rb_tree", "std::_Rb_tree_node<A>"));
  EXPECT_TRUE(TypeTreeStoreTypeTreeForContainerHasNodeWithTypeName(
      type_tree_store, "std::_Rb_tree", "std::_Rb_tree_node<A>", "A"));
  EXPECT_TRUE(TypeTreeStoreHasTypeTreeForContainer(
      type_tree_store, "std::_Rb_tree",
      "std::_Rb_tree_node<std::pair<const A, A> >"));
  EXPECT_TRUE(TypeTreeStoreTypeTreeForContainerHasNodeWithTypeName(
      type_tree_store, "std::_Rb_tree",
      "std::_Rb_tree_node<std::pair<const A, A> >", "A"));

  EXPECT_TRUE(TypeTreeStoreHasTypeTreeForContainer(
      type_tree_store, "std::__cxx11::_List_base", "std::_List_node<A>"));
  EXPECT_TRUE(TypeTreeStoreTypeTreeForContainerHasNodeWithTypeName(
      type_tree_store, "std::__cxx11::_List_base", "std::_List_node<A>", "A"));
  EXPECT_TRUE(TypeTreeStoreHasTypeTreeForContainer(
      type_tree_store, "std::_Fwd_list_base", "std::_Fwd_list_node<A>"));
  EXPECT_TRUE(TypeTreeStoreTypeTreeForContainerHasNodeWithTypeName(
      type_tree_store, "std::_Fwd_list_base", "std::_Fwd_list_node<A>", "A"));
  EXPECT_TRUE(TypeTreeStoreHasTypeTreeForContainer(
      type_tree_store, "std::__cxx11::basic_string", "char"));
  EXPECT_TRUE(TypeTreeStoreHasTypeTreeForContainer(
      type_tree_store, "std::__detail::_Hashtable_alloc",
      "std::__detail::_Hash_node<std::pair<const A, A>, false>"));
  EXPECT_TRUE(TypeTreeStoreTypeTreeForContainerHasNodeWithTypeName(
      type_tree_store, "std::__detail::_Hashtable_alloc",
      "std::__detail::_Hash_node<std::pair<const A, A>, false>", "A"));
  EXPECT_TRUE(TypeTreeStoreHasTypeTreeForContainer(
      type_tree_store, "std::__detail::_Hashtable_alloc",
      "std::__detail::_Hash_node<A, false>"));
  EXPECT_TRUE(TypeTreeStoreTypeTreeForContainerHasNodeWithTypeName(
      type_tree_store, "std::__detail::_Hashtable_alloc",
      "std::__detail::_Hash_node<A, false>", "A"));
  EXPECT_TRUE(TypeTreeStoreHasTypeTreeForContainer(
      type_tree_store, "std::__detail::_Hashtable_alloc",
      "std::__detail::_Hash_node<std::pair<const A, A>, false>"));
  EXPECT_TRUE(TypeTreeStoreTypeTreeForContainerHasNodeWithTypeName(
      type_tree_store, "std::__detail::_Hashtable_alloc",
      "std::__detail::_Hash_node<std::pair<const A, A>, false>", "A"));
  EXPECT_TRUE(TypeTreeStoreHasTypeTreeForContainer(
      type_tree_store, "std::__detail::_Hashtable_alloc",
      "std::__detail::_Hash_node<A, false>"));
  EXPECT_TRUE(TypeTreeStoreTypeTreeForContainerHasNodeWithTypeName(
      type_tree_store, "std::__detail::_Hashtable_alloc",
      "std::__detail::_Hash_node<A, false>", "A"));
  EXPECT_TRUE(TypeTreeStoreHasTypeTreeForContainer(
      type_tree_store, "std::__detail::_Hashtable_alloc",
      "std::__detail::_Hash_node<A, false>"));
  EXPECT_TRUE(TypeTreeStoreTypeTreeForContainerHasNodeWithTypeName(
      type_tree_store, "std::__detail::_Hashtable_alloc",
      "std::__detail::_Hash_node_base*", "std::__detail::_Hash_node_base*"));
}

// This test checks that the histogram builder can correctly build a histogram
// for all the supported Abseil containers.
TEST(HistogramBuilderTest, SupportedAbseilContainersTest) {
  const std::string exe_path = blaze_util::JoinPath(
      kHistogramBuilderTestPath, "supported_abseil_containers.exe");
  const std::string profile_path = blaze_util::JoinPath(
      kHistogramBuilderTestPath, "supported_abseil_containers.memprofraw");

  // We use only_records=true to filter out the metadata allocations from the
  // slot allocations in the hash containers.
  ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<AbstractHistogramBuilder> histogram_builder,
      LocalHistogramBuilder::Create(
          profile_path, exe_path, exe_path, /*type_prefix_filter=*/{},
          /*callstack_filter=*/{}, /*only_records=*/true,
          /*verify_verbose=*/false, /*dump_unresolved_callstacks=*/false));
  ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<HistogramBuilderResults> histogram_builder_results,
      histogram_builder->BuildHistogram());

  const TypeTreeStore* type_tree_store =
      histogram_builder_results->type_tree_store.get();

  EXPECT_EQ(type_tree_store->callstack_to_type_tree_.size(), 6);
  for (const auto& [callstack, type_tree] :
       type_tree_store->callstack_to_type_tree_) {
    EXPECT_TRUE(TypeTreeHasNodeWithTypeName(type_tree.get(), "A"));
  }

  // Four different hash containers with the same container internal type:
  // flat_hash_set, flat_hash_map, node_hash_set, node_hash_map.
  EXPECT_EQ(GetNumTypeTreesForContainer(
                type_tree_store, "absl::container_internal::raw_hash_set"),
            2);
  // Four different btree containers with the same container internal type:
  // btree_set, btree_map, btree_multiset, btree_multimap.
  EXPECT_EQ(GetNumTypeTreesForContainer(type_tree_store,
                                        "absl::container_internal::btree"),
            4);
}

// This test checks that the histogram builder can correctly build a histogram
// for all the supported ADT containers.
TEST(HistogramBuilderTest, SupportedADTContainersTest) {
  const std::string exe_path = blaze_util::JoinPath(
      kHistogramBuilderTestPath, "supported_adt_containers.exe");
  const std::string profile_path = blaze_util::JoinPath(
      kHistogramBuilderTestPath, "supported_adt_containers.memprofraw");

  ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<AbstractHistogramBuilder> histogram_builder,
      LocalHistogramBuilder::Create(profile_path, exe_path, exe_path,
                                    /*type_prefix_filter=*/{},
                                    /*callstack_filter=*/{},
                                    /*only_records=*/false,
                                    /*verify_verbose=*/false,
                                    /*dump_unresolved_callstacks=*/false));
  ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<HistogramBuilderResults> histogram_builder_results,
      histogram_builder->BuildHistogram());

  const TypeTreeStore* type_tree_store =
      histogram_builder_results->type_tree_store.get();
  EXPECT_TRUE(TypeTreeStoreHasTypeTreeForContainer(
      type_tree_store, "llvm::SmallVectorTemplateBase", "A"));
  EXPECT_TRUE(TypeTreeStoreTypeTreeForContainerHasNodeWithTypeName(
      type_tree_store, "llvm::SmallVectorTemplateBase", "A", "A"));
  EXPECT_TRUE(TypeTreeStoreHasTypeTreeForContainer(type_tree_store,
                                                   "llvm::PagedVector", "A"));
  EXPECT_TRUE(TypeTreeStoreTypeTreeForContainerHasNodeWithTypeName(
      type_tree_store, "llvm::PagedVector", "A", "A"));
  EXPECT_TRUE(TypeTreeStoreHasTypeTreeForContainer(
      type_tree_store, "llvm::DenseMapBase",
      "llvm::detail::DenseMapPair<A, unsigned int>"));
  EXPECT_TRUE(TypeTreeStoreTypeTreeForContainerHasNodeWithTypeName(
      type_tree_store, "llvm::DenseMapBase",
      "llvm::detail::DenseMapPair<A, unsigned int>", "A"));
  EXPECT_TRUE(TypeTreeStoreHasTypeTreeForContainer(
      type_tree_store, "llvm::DenseMapBase", "llvm::detail::DenseSetPair<A>"));
  EXPECT_TRUE(TypeTreeStoreTypeTreeForContainerHasNodeWithTypeName(
      type_tree_store, "llvm::DenseMapBase", "llvm::detail::DenseSetPair<A>",
      "A"));
}
}  // namespace
}  // namespace devtools_crosstool_fdo_field_access
