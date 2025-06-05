/*
 * Copyright 2025 Google LLC
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef TYPE_TREE_H_
#define TYPE_TREE_H_
#include <stdbool.h>
#include <sys/types.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <numeric>
#include <ostream>
#include <string>
#include <utility>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "dwarf_metadata_fetcher.h"
#include "src/object_layout.pb.h"

namespace devtools_crosstool_fdo_field_access {

class TypeTree {
 public:
  struct AccessCounters {
    enum AccessType {
      kAccess = 0, /*Load or store.*/
      kLlcMiss = 1,
    };
    uint64_t total = 0;
    uint64_t access = 0;
    uint64_t llc_miss = 0;
  };

  static constexpr uint32_t kDefaultAccessGranularity = 8;
  static constexpr TypeTree::AccessCounters::AccessType kDefaultAccessType =
      AccessCounters::kAccess;

  struct Node {
    explicit Node(absl::string_view name, absl::string_view type_name,
                  int64_t offset_bits, int64_t size_bits, int64_t multiplicity,
                  ObjectLayout::Properties::TypeKind type_kind,
                  ObjectLayout::Properties::ObjectKind object_kind,
                  int64_t global_offset,
                  AccessCounters access_counters = {0, 0, 0},
                  bool is_union = false);

    static std::unique_ptr<Node> CreateNodeFromTypedata(
        absl::string_view name, absl::string_view type_name,
        int64_t offset_bits, int64_t multiplicity,
        const DwarfMetadataFetcher::TypeData* type_data,
        const Node* parent_node) {
      return std::make_unique<Node>(
          name, type_name, offset_bits, type_data->size * 8, multiplicity,
          DwarfTypeKindToObjectTypeKind(type_data->data_type),
          multiplicity > 1 ? ObjectLayout::Properties::ARRAY_ELEMENTS
                           : ObjectLayout::Properties::FIELD,
          parent_node ? parent_node->GetGlobalOffsetBits() + offset_bits : 0,
          AccessCounters(),
          type_data->data_type == DwarfMetadataFetcher::DataType::UNION);
    }

    static std::unique_ptr<Node> CreateArrayTypeNode(
        absl::string_view name, absl::string_view type_name, int64_t size_bits,
        int64_t offset_bits, int64_t multiplicity, const Node* parent_node) {
      return std::make_unique<Node>(
          name, type_name, offset_bits, size_bits, multiplicity,
          ObjectLayout::Properties::ARRAY_TYPE,
          multiplicity > 1 ? ObjectLayout::Properties::ARRAY_ELEMENTS
                           : ObjectLayout::Properties::FIELD,
          parent_node ? parent_node->GetGlobalOffsetBits() + offset_bits : 0);
    }

    static std::unique_ptr<Node> CreateRootNode(
        absl::string_view type_name,
        const DwarfMetadataFetcher::TypeData* type_data) {
      return std::make_unique<Node>(
          type_name, type_name, 0, type_data->size * 8,
          /*multiplicity=*/1,
          DwarfTypeKindToObjectTypeKind(type_data->data_type),
          ObjectLayout::Properties::FIELD,
          /*global_offset=*/0, AccessCounters(),
          /*is_union=*/type_data->data_type ==
              DwarfMetadataFetcher::DataType::UNION);
    }

    static std::unique_ptr<Node> CreateNodeFromObjectLayout(
        const ObjectLayout& object_layout, const Node* parent_node) {
      return std::make_unique<Node>(
          object_layout.properties().name(),
          object_layout.properties().type_name(),
          object_layout.properties().offset_bits(),
          object_layout.properties().size_bits(),
          object_layout.properties().multiplicity(),
          object_layout.properties().type_kind(),
          object_layout.properties().kind(),
          !parent_node ? 0
                       : parent_node->GetGlobalOffsetBits() +
                             object_layout.properties().offset_bits());
    }

    static std::unique_ptr<Node> CreatePaddingNode(int64_t from_offset,
                                                   int64_t to_offset,
                                                   const Node* parent_node) {
      return std::make_unique<Node>(
          "", "", from_offset, to_offset - from_offset, /*multiplicity=*/1,
          ObjectLayout::Properties::PADDING_TYPE,
          ObjectLayout::Properties::PADDING,
          parent_node ? parent_node->GetGlobalOffsetBits() + from_offset : 0);
    }

    static std::unique_ptr<Node> CreateUnresolvedTypeNode(
        absl::string_view name, absl::string_view type_name,
        int64_t offset_bits, int64_t multiplicity, int64_t inferred_size,
        const Node* parent_node) {
      return std::make_unique<Node>(
          name, type_name, offset_bits, inferred_size, multiplicity,
          ObjectLayout::Properties::UNKNOWN_TYPE,
          ObjectLayout::Properties::UNKNOWN,
          parent_node ? parent_node->GetGlobalOffsetBits() + offset_bits : 0);
    }

    static std::unique_ptr<Node> CreatePointerNode(absl::string_view name,
                                                   absl::string_view type_name,
                                                   int64_t offset_bits,
                                                   int64_t multiplicity,
                                                   int64_t pointer_size,
                                                   const Node* parent_node) {
      return std::make_unique<Node>(
          name, type_name, offset_bits, pointer_size, multiplicity,
          ObjectLayout::Properties::INDIRECTION_TYPE,
          multiplicity > 1 ? ObjectLayout::Properties::ARRAY_ELEMENTS
                           : ObjectLayout::Properties::FIELD,
          parent_node ? parent_node->GetGlobalOffsetBits() + offset_bits : 0);
    }

    // Creates a copy of the values in the node without the children.
    static std::unique_ptr<Node> CopyNode(const Node& node) {
      return std::make_unique<Node>(
          node.object_layout.properties().name(),
          node.object_layout.properties().type_name(),
          node.object_layout.properties().offset_bits(),
          node.object_layout.properties().size_bits(),
          node.object_layout.properties().multiplicity(),
          node.object_layout.properties().type_kind(),
          node.object_layout.properties().kind(), node.global_offset,
          node.access_counters);
    }

    void AddChild(std::unique_ptr<Node> node);
    void AddChildAndInsertPaddingIfNecessary(
        std::unique_ptr<Node> child, const TypeTree::Node* parent_node,
        uint32_t field_index,
        const std::vector<const DwarfMetadataFetcher::FieldData*>&
            resolved_fields);
    const Node* GetChild(uint32_t Idx) const;
    size_t NumChildren() const { return children.size(); }
    int64_t GetGlobalOffsetBits() const { return global_offset; }
    int64_t GetGlobalOffsetBytes() const { return global_offset / 8; }
    uint64_t GetTotalAccessCount() const { return access_counters.total; }
    void SetGlobalOffsetBits(int64_t offset) { global_offset = offset; }

    int64_t GetOffsetBits() const {
      return object_layout.properties().offset_bits();
    }
    int64_t GetOffsetBytes() const {
      return object_layout.properties().offset_bits() / 8;
    }
    int64_t GetSizeBits() const {
      return object_layout.properties().size_bits();
    }
    int64_t GetSizeBytes() const {
      return object_layout.properties().size_bits() / 8;
    }
    int64_t GetFullSizeBits() const {
      return object_layout.properties().size_bits() *
             object_layout.properties().multiplicity();
    }
    int64_t GetFullSizeBytes() const {
      return object_layout.properties().size_bits() *
             object_layout.properties().multiplicity() / 8;
    }
    void SetSizeBits(int64_t size_bits) {
      return object_layout.mutable_properties()->set_size_bits(size_bits);
    }
    int64_t GetMultiplicity() const {
      return object_layout.properties().multiplicity();
    }
    absl::string_view GetName() const {
      return object_layout.properties().name();
    }
    absl::string_view GetTypeName() const {
      return object_layout.properties().type_name();
    }
    ObjectLayout::Properties::TypeKind GetTypeKind() const {
      return object_layout.properties().type_kind();
    }
    ObjectLayout GetObjectLayout() const { return object_layout; }
    bool IsPadding() const {
      return object_layout.properties().type_kind() ==
             ObjectLayout::Properties::PADDING_TYPE;
    }
    bool IsIndirectionType() const {
      return object_layout.properties().type_kind() ==
             ObjectLayout::Properties::INDIRECTION_TYPE;
    }
    bool IsUnresolvedType() const {
      return GetTypeKind() == ObjectLayout::Properties::UNKNOWN_TYPE;
    }

    bool IsUnion() const { return is_union; }

    bool IsArrayType() const {
      return GetTypeKind() == ObjectLayout::Properties::ARRAY_TYPE;
    }
    bool IsRecordType() const {
      return GetTypeKind() == ObjectLayout::Properties::RECORD_TYPE;
    }

    uint64_t GetSubtreeSize() const;

    void Dump(std::ostream& out, int level,
              bool dump_full_unions = false) const;
    void DumpFlameGraph(std::ostream& out, std::vector<std::string>& path,
                        const std::string& root_name) const;
    void CreateChildFromSuboject(const ObjectLayout& object_layout);
    void CreateObjectLayoutFromChildren(ObjectLayout& object_layout) const;
    absl::string_view NameToString(absl::string_view name) const;

    absl::Status MergeCounts(const Node* other);
    template <AccessCounters::AccessType AccessType>
    void IncrementAccessCount(uint64_t count);
    template <uint32_t AccessGranularity, AccessCounters::AccessType AccessType>
    bool RecordAccess(int64_t offset_bytes, uint64_t count) {
      const std::vector<uint64_t> array_element_offsets = {0};
      return RecordAccess<AccessGranularity, AccessType>(offset_bytes, count,
                                                         array_element_offsets);
    }

    absl::StatusOr<const TypeTree::Node*> FindNodeWithTypeName(
        absl::string_view type_name) const;
    absl::Status MergeTreeIntoThis(const Node* other, int64_t starting_offset);
    void InferOffsetsFromSizes();
    void BuildSizesBottomUp();

    bool Verify(const Node* parent, const Node* older_sibling,
                bool verify_verbose) const;
    friend std::ostream& operator<<(std::ostream& os, const Node& node);

   protected:
    // Node is a wrapper for ObjectLayout. While ObjectLayout has repeated
    // field subobjects, we use the Node.children to represent subobjects, so
    // we can associate counters with each subobject.
    ObjectLayout object_layout;
    int64_t global_offset;
    AccessCounters access_counters;
    std::vector<std::unique_ptr<Node>> children;
    bool is_union;

   private:
    template <uint32_t AccessGranularity, AccessCounters::AccessType AccessType>
    bool RecordAccess(int64_t offset_bytes, uint64_t count,
                      const std::vector<uint64_t>& array_element_offsets);
  };

  static ObjectLayout::Properties::TypeKind DwarfTypeKindToObjectTypeKind(
      DwarfMetadataFetcher::DataType data_type);

  static std::unique_ptr<TypeTree> CreateTreeFromObjectLayout(
      const ObjectLayout& object_layout, std::string root_type_name,
      std::string container_name = "");

  static ObjectLayout CreateObjectLayoutFromTree(const TypeTree& type_tree);

  static absl::string_view TypeKindToString(
      ObjectLayout::Properties::TypeKind type_kind);

  //  TODO(b/352368491): This function collapses a histogram into a smaller
  //  histogram. The collapsed size should match the size of the targeted type
  //  tree. There may be misalignment between the histogram and the type tree if
  //  the histogram granularity does not match the alignment of the allocated
  //  type.
  template <uint32_t AccessGranularity>
  static std::vector<uint64_t> CollapseHistogram(
      const std::vector<uint64_t>& histogram, int64_t collapsed_size);

  explicit TypeTree(std::unique_ptr<Node> root,
                    absl::string_view root_type_name, bool from_container,
                    absl::string_view container_name)
      : root_(std::move(root)),
        root_type_name_(root_type_name),
        from_container_(from_container),
        container_name_(container_name) {}
  ~TypeTree() = default;
  TypeTree(const TypeTree&) = delete;
  TypeTree& operator=(const TypeTree&) = delete;
  bool operator==(const TypeTree& other) const {
    return root_type_name_ == other.root_type_name_;
  }
  void Dump(std::ostream& out, int level = 0,
            bool dump_full_unions = false) const;
  void DumpFlameGraph(std::ostream& out, uint64_t id = 0) const;
  // This function is used to verify the tree structure. It will return true
  // if the tree is valid. If verify_verbose is true, it will print out the
  // error message and the node which has a mistake. The main properties that
  // this function guarantees are:
  //
  // (1) Access counters of a parent is larger than the sum of access counters
  // of the children.
  // (2) For any node, the offset of the next sibling is equal to the offset of
  // the current node plus the size of the current node. If it is the last child
  // of its parent, it guarantees that the current offset plus the current size
  // is equal to the parents size.
  bool Verify(bool verify_verbose = false) const {
    return root_->Verify(nullptr, nullptr, verify_verbose);
  }

  // TODO(b/354286463): Specialize this function for abseil containers when
  // recording accesses from gwp.
  template <uint32_t AccessGranularity = kDefaultAccessGranularity,
            AccessCounters::AccessType AccessType = kDefaultAccessType>
  bool RecordAccess(int64_t offset_bytes, uint64_t count = 1);

  template <uint32_t AccessGranularity = kDefaultAccessGranularity,
            AccessCounters::AccessType AccessType = kDefaultAccessType>
  absl::Status RecordAccessHistogram(uint64_t* histogram,
                                     uint32_t histogram_size) {
    std::vector<uint64_t> histogram_vector(histogram,
                                           histogram + histogram_size);
    return RecordAccessHistogram<AccessGranularity, AccessType>(
        histogram_vector);
  }
  template <uint32_t AccessGranularity = kDefaultAccessGranularity,
            AccessCounters::AccessType AccessType = kDefaultAccessType>
  absl::Status RecordAccessHistogram(std::vector<uint64_t>& histogram);
  absl::Status MergeTreeIntoThis(const TypeTree* other);
  absl::StatusOr<const TypeTree::Node*> FindNodeWithTypeName(
      absl::string_view type_name) const;
  void InferOffsetsFromSizes() {
    root_->SetGlobalOffsetBits(0);
    root_->InferOffsetsFromSizes();
  }
  void BuildSizesBottomUp() { root_->BuildSizesBottomUp(); }
  bool Empty() const { return root_ == nullptr; }
  bool IsRecordType() const { return root_->IsRecordType(); }
  bool FromContainer() const { return from_container_; }
  absl::Status MergeCounts(const TypeTree* other);
  absl::string_view Name() const { return root_type_name_; }
  absl::string_view ContainerName() const { return container_name_; }
  const Node* Root() const { return root_.get(); }

 private:
  std::unique_ptr<Node> root_;
  const std::string root_type_name_;
  // Whether the type tree is from an allocation made within a container.
  bool from_container_;
  // Name of the container that the type tree is from. Should selected from the
  // supported containers list. Empty if the type tree is not from a container.
  std::string container_name_;
};

// This class is used to store the histogram of field accesses. This is a flat
// representation of a TypeTree, with only the leaf fields being represented.
// This should be interpreted as a "view" of a type tree, that is visually more
// representative of where and at what offsets the accesses are made and helpful
// for analysis and debugging large TypeTrees.
class FieldAccessHistogram {
 public:
  static absl::StatusOr<std::unique_ptr<FieldAccessHistogram>> Create(
      const TypeTree* type_tree);

  int64_t GetSizeBytes() const { return size_in_bits_ / 8; }
  int64_t GetSizeBits() const { return size_in_bits_; }
  void Dump(std::ostream& out) const;

  explicit FieldAccessHistogram(absl::string_view root_type_name,
                                int64_t size_in_bits)
      : root_type_name_(root_type_name), size_in_bits_(size_in_bits) {}
  ~FieldAccessHistogram() = default;
  FieldAccessHistogram() = delete;
  FieldAccessHistogram(const FieldAccessHistogram&) = delete;
  FieldAccessHistogram& operator=(const FieldAccessHistogram&) = delete;

  std::string root_type_name_;
  int64_t size_in_bits_;
  // Maps global offset of field in the type to the index of the node.
  absl::flat_hash_map<int64_t, uint64_t> offset_to_idx_;
  std::vector<std::unique_ptr<TypeTree::Node>> nodes_;
};

// Template functions here so that the compiler can instantiate them.
inline bool Overlap(int64_t a1, int64_t a2, int64_t b1, int64_t b2) {
  return std::max(a2, b2) - std::min(a1, b1) < (a2 - a1) + (b2 - b1);
}

template <uint32_t AccessGranularity,
          TypeTree::AccessCounters::AccessType AccessType>
bool TypeTree::Node::RecordAccess(
    int64_t offset_bytes, uint64_t count,
    const std::vector<uint64_t>& array_element_offsets) {
  // First check if there is any overlap possibility in the largest range of the
  // current node. We don't need to add counts or, most importantly, continue
  // recursively if there is no overlap.
  // std::cout << " offset " << offset_bytes << " access granularity "
  //           << AccessGranularity << " global offset " <<
  //           GetGlobalOffsetBytes()
  //           << " max offset "
  //           << GetGlobalOffsetBytes() + array_element_offsets.back() +
  //                  GetFullSizeBytes()
  //           << " "
  //           << " node: " << this->object_layout.properties().name() << "| ";
  if (!Overlap(offset_bytes, offset_bytes + AccessGranularity,
               GetGlobalOffsetBytes(),
               GetGlobalOffsetBytes() + array_element_offsets.back() +
                   GetFullSizeBytes())) {
    // std::cout << "no overlap" << std::endl;
    return false;
  }
  // std::cout << "overlap" << std::endl;

  // For each array element offset (explained below), check if there is any
  // overlap with the current node. If there is, add the count to the access
  // count of the node.
  for (uint64_t array_element_offset : array_element_offsets) {
    uint64_t base = GetGlobalOffsetBytes() + array_element_offset;
    if (Overlap(offset_bytes, offset_bytes + AccessGranularity, base,
                base + GetFullSizeBytes())) {
      IncrementAccessCount<AccessType>(count);
    }
  }

  // The following approach is similar to a backtracking recursive algorithm.
  // Here we do the work required for the next descendants.

  // Whenever a node is an array type, the node occurs only a single time in the
  // type tree, but has a multiplicity denoting the number of times the element
  // occurs inside the array. Intuitively, for tracking field access counts this
  // means if there is an embedded struct inside the array, the field is
  // duplicated by the number of elements in the array at constant intervals of
  // the array element size. This is repeated for each recursively embedded
  // struct inside an array (see ArrayAccessCountTest for examples). To properly
  // track field access counts, we need to be aware of the size and multiplicity
  // of every ancestor node.

  // More formally, given a node n with ancestors a_1, a_2, ..., a_k with
  // multiplicities m_1, m_2, ..., m_k and sizes s_1, s_2, ..., s_k, we must
  // compute the offset for all combinations of multiplicities:
  // ArrayElementOffset :=
  // {{x * s_1 | 0 <= x < m_1} + ... + {x * s_k | 0 <=x <m_k}}.
  // A node n is duplicated at each offset: {o + GlobalOffset(n) | o in
  // ArrayElementOffset }.

  // To achieve this, we take each element o of the ArrayElementOffset from the
  // parent and create m new elements {x * s + o| 0 <= x < m}, where m is the
  // multiplicity current node and s is the size of the current node.
  std::vector<uint64_t> new_array_element_offsets;
  new_array_element_offsets.reserve(array_element_offsets.size() *
                                    GetMultiplicity());
  for (int i = 0; i < GetMultiplicity(); i++) {
    for (uint64_t array_element_offset : array_element_offsets) {
      new_array_element_offsets.push_back(array_element_offset +
                                          i * GetSizeBytes());
    }
  }

  bool overlap_in_children = false | children.empty();
  for (auto& child : children) {
    overlap_in_children |= child->RecordAccess<AccessGranularity, AccessType>(
        offset_bytes, count, new_array_element_offsets);
  }
  if (!overlap_in_children) {
    return false;
  } else {
    return true;
  }
}

template <uint32_t AccessGranularity,
          TypeTree::AccessCounters::AccessType AccessType>
bool TypeTree::RecordAccess(int64_t offset_bytes, uint64_t count) {
  // std::cout << "RecordAccess: " << offset_bytes << " " << count << std::endl;
  if (offset_bytes > root_->GetFullSizeBytes()) {
    offset_bytes = offset_bytes % root_->GetFullSizeBytes();
  }
  return root_->RecordAccess<AccessGranularity, AccessType>(offset_bytes,
                                                            count);
}
template <uint32_t AccessGranularity>
std::vector<uint64_t> TypeTree::CollapseHistogram(
    const std::vector<uint64_t>& histogram, int64_t collapsed_size) {
  uint32_t histogram_size = histogram.size();
  uint32_t new_histogram_size = 1 + (collapsed_size - 1) / AccessGranularity;
  uint32_t collapse_num = histogram_size / new_histogram_size;
  std::vector<uint64_t> collapsed_histogram(new_histogram_size, 0);
  for (uint32_t i = 0; i < collapse_num; i++) {
    for (uint32_t j = 0; j < new_histogram_size; j++) {
      collapsed_histogram[j] += histogram[i * new_histogram_size + j];
    }
  }
  return collapsed_histogram;
}

template <uint32_t AccessGranularity,
          TypeTree::AccessCounters::AccessType AccessType>
absl::Status TypeTree::RecordAccessHistogram(std::vector<uint64_t>& histogram) {
  uint32_t old_histogram_size = histogram.size();
  uint64_t histogram_size_in_bytes = histogram.size() * AccessGranularity;
  if (histogram_size_in_bytes == 0) {
    return absl::InvalidArgumentError("Histogram size is 0");
  }

  if (AccessGranularity != 8U) {
    return absl::UnimplementedError(
        "Access granularity must be 8 bytes for now");
  }

  if (histogram_size_in_bytes > root_->GetFullSizeBytes() &&
      histogram_size_in_bytes < 2 * root_->GetFullSizeBytes()) {
    // This means the histogram is larger than the type, but we do not have a
    // bulk allocation. We may continue without collapsing.
  } else if (histogram_size_in_bytes > this->Root()->GetFullSizeBytes()) {
    histogram = CollapseHistogram<AccessGranularity>(
        histogram, this->Root()->GetFullSizeBytes());
  }

  for (uint32_t i = 0; i < histogram.size(); ++i) {
    root_->RecordAccess<AccessGranularity, AccessType>(i * AccessGranularity,
                                                       histogram[i]);
  }

  // TODO(b/352368491): Investigate some scenarios where the histogram size is
  // not a multiple of the type size.
  if (old_histogram_size % histogram.size() != 0) {
    return absl::FailedPreconditionError(absl::StrCat(
        "condition failed: histogram_size % new_histogram_size != 0 ",
        old_histogram_size, " % ", histogram.size(),
        " == ", old_histogram_size % histogram.size()));
  }
  return absl::OkStatus();
}

template <TypeTree::AccessCounters::AccessType AccessType>
void TypeTree::Node::IncrementAccessCount(uint64_t count) {
  access_counters.total += count;
  if constexpr (AccessType == AccessCounters::kAccess) {
    access_counters.access += count;
  } else if constexpr (AccessType == AccessCounters::kLlcMiss) {
    access_counters.llc_miss += count;
  } else {
    static_assert(false && "Unknown access type");
  }
}

}  // namespace devtools_crosstool_fdo_field_access

#endif  // TYPE_TREE_H_
