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

#include "src/type_tree.h"

#include <sys/types.h>

#include <cstddef>
#include <cstdint>
#include <iostream>
#include <memory>
#include <ostream>
#include <queue>
#include <string>
#include <utility>
#include <vector>

#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "dwarf_metadata_fetcher.h"
#include "src/object_layout.pb.h"
#include "status_macros.h"

namespace devtools_crosstool_fdo_field_access {

namespace {
void DumpLevel(std::ostream &out, int level) {
  for (size_t i = 0; i < level; ++i) {
    out << "  ";
  }
}
}  // namespace

void TypeTree::Node::AddChild(std::unique_ptr<Node> node) {
  children.push_back(std::move(node));
}

void TypeTree::Node::AddChildAndInsertPaddingIfNecessary(
    std::unique_ptr<Node> child, const TypeTree::Node *parent_node,
    uint32_t field_index,
    const std::vector<const DwarfMetadataFetcher::FieldData *>
        &resolved_fields) {
  // For now, if we have a union, we don't insert padding. This is because the
  // size of a union is determined by the maximum size of any possible type
  // inside the union.
  if (parent_node->IsUnion()) {
    children.push_back(std::move(child));
    return;
  }

  if (field_index > 0) {
    int64_t last_end = resolved_fields[field_index - 1]->offset * 8 +
                       parent_node->GetChild(parent_node->NumChildren() - 1)
                           ->GetFullSizeBits();
    int64_t current_start = resolved_fields[field_index]->offset * 8;
    if (current_start > last_end) {
      // There is a gap between the last field and the current field.
      // Insert a padding node.
      children.push_back(
          CreatePaddingNode(last_end, current_start, parent_node));
    }
  }

  std::unique_ptr<Node> padding_node = nullptr;
  if (field_index == resolved_fields.size() - 1 &&
      parent_node->GetSizeBits() >
          child->GetOffsetBits() + child->GetFullSizeBits()) {
    // We have a gap between the last field and the end of the parent node.
    // Insert a padding node.
    padding_node =
        CreatePaddingNode(child->GetOffsetBits() + child->GetFullSizeBits(),
                          parent_node->GetSizeBits(), parent_node);
  }
  children.push_back(std::move(child));

  if (padding_node) {
    children.push_back(std::move(padding_node));
  }
}

const TypeTree::Node *TypeTree::Node::GetChild(uint32_t Idx) const {
  return children[Idx].get();
}

absl::string_view TypeTree::Node::NameToString(absl::string_view name) const {
  if (IsPadding()) {
    return "/*padding*/";
  } else {
    return name;
  }
}

uint64_t TypeTree::Node::GetSubtreeSize() const {
  uint64_t result = 1;
  for (const auto &child : children) {
    result += child->GetSubtreeSize();
  }
  return result;
}

void TypeTree::Node::Dump(std::ostream &out, int level,
                          bool dump_full_unions) const {
  DumpLevel(out, level - 1);

  out << "- type:   " << NameToString(GetTypeName());
  if (IsUnresolvedType()) {
    out << " (Unresolved)";
  }

  if (IsUnion()) {
    out << " (Union)";
  }

  out << "\n";

  if (level > 1 && !IsPadding()) {
    DumpLevel(out, level);
    out << "name:   " << NameToString(GetName()) << "\n";
  }
  DumpLevel(out, level);
  out << "size:   " << GetSizeBytes() << "\n";
  if (GetMultiplicity() > 1) {
    DumpLevel(out, level);
    out << "multiplicity: " << GetMultiplicity() << "\n";
  }
  DumpLevel(out, level);
  out << "total_access: " << GetTotalAccessCount() << "\n";
  DumpLevel(out, level);
  out << "global_offset: " << GetGlobalOffsetBytes() << "\n";
  if (!children.empty()) {
    DumpLevel(out, level);
    out << "children: " << "\n";
    // Print only the child of the union with the larger tree size. This is
    // heuristic to show which one is more likely to contain more relevant
    // information.
    if (!dump_full_unions && IsUnion() && !children.empty()) {
      const Node *biggest_child = GetChild(0);
      for (int i = 1; i < children.size(); ++i) {
        const Node *child = GetChild(i);
        if (child->GetSubtreeSize() > biggest_child->GetSubtreeSize()) {
          biggest_child = child;
        }
      }
      biggest_child->Dump(out, level + 1, dump_full_unions);
    } else {
      for (auto &child : children) {
        child->Dump(out, level + 1, dump_full_unions);
      }
    }
  }
}

void TypeTree::Dump(std::ostream &out, int level, bool dump_full_unions) const {
  if (!Empty()) {
    DumpLevel(out, level);
    out << "container: ";
    if (FromContainer()) {
      out << ContainerName() << "\n";
    } else {
      out << "<none>" << "\n";
    }
    DumpLevel(out, level);
    out << "tree: \n";
    root_->Dump(out, level + 1);
  }
}

void TypeTree::DumpFlameGraph(std::ostream &out, uint64_t id) const {
  std::string root_name =
      absl::StrCat(container_name_, id == 0 ? "" : absl::StrCat("", id));
  // std::string root_name = absl::StrCat(container_name_, (id == 0 ?  : id));
  std::vector<std::string> path;
  root_->DumpFlameGraph(out, path, root_name);
}

void TypeTree::Node::DumpFlameGraph(std::ostream &out,
                                    std::vector<std::string> &path,
                                    const std::string &root_name) const {
  // out << "<" << root_name << ">";
  out << root_name << "_";
  std::string name = "";
  absl::StrAppend(&name, GetOffsetBytes(), "|", NameToString(GetTypeName()),
                  "|", GetName());
  for (auto &p : path) {
    out << p << ";";
  }
  out << name << " " << (NumChildren() > 0 ? 0 : GetTotalAccessCount()) << "\n";
  std::vector<std::string> new_path = path;
  new_path.push_back(name);
  for (auto &child : children) {
    child->DumpFlameGraph(out, new_path, root_name);
  }
}

TypeTree::Node::Node(absl::string_view name, absl::string_view type_name,
                     int64_t offset_bits, int64_t size_bits,
                     int64_t multiplicity,
                     ObjectLayout::Properties::TypeKind type_kind,
                     ObjectLayout::Properties::ObjectKind object_kind,
                     int64_t global_offset,
                     TypeTree::AccessCounters access_counters, bool is_union)
    : global_offset(global_offset),
      access_counters(access_counters),
      is_union(is_union) {
  object_layout.mutable_properties()->set_name(name);
  object_layout.mutable_properties()->set_type_name(type_name);
  object_layout.mutable_properties()->set_offset_bits(offset_bits);
  object_layout.mutable_properties()->set_size_bits(size_bits);
  object_layout.mutable_properties()->set_multiplicity(multiplicity);
  object_layout.mutable_properties()->set_type_kind(type_kind);
  object_layout.mutable_properties()->set_kind(object_kind);
}

bool TypeTree::Node::Verify(const Node *parent, const Node *older_sibling,
                            bool verify_verbose) const {
  bool res = true;

  if (parent != nullptr && parent->IsUnion()) {
    // All children of a union should have offset 0.
    if (this->GetOffsetBytes() != 0 && !this->IsPadding()) {
      if (verify_verbose) {
        LOG(ERROR) << "Union child offset not 0 where parent is union: "
                   << GetOffsetBytes() << " != 0 for " << "\n"
                   << *this << "\n";
      }
      res = false;
    }

    // A child of a union should have the same access count as the parent if
    // there is only one child.
    if (older_sibling == nullptr) {
      if (parent != nullptr && parent->NumChildren() == 1 &&
          this->GetTotalAccessCount() != parent->GetTotalAccessCount()) {
        if (verify_verbose) {
          LOG(ERROR) << "Union child access count mismatch: "
                     << this->GetTotalAccessCount()
                     << " != " << parent->GetTotalAccessCount() << " for "
                     << "\n"
                     << *this << "\n";
        }
        res = false;
      }
    } else {
      // A child of a union should have the same access count as other children
      // if the size is the same.
      if (this->GetFullSizeBytes() == older_sibling->GetFullSizeBytes() &&
          this->GetTotalAccessCount() != older_sibling->GetTotalAccessCount()) {
        if (verify_verbose) {
          LOG(ERROR) << "Union child access count mismatch even though size is "
                        "the same: "
                     << this->GetTotalAccessCount()
                     << " != " << parent->GetTotalAccessCount() << " for "
                     << "\n"
                     << *this << "\n";
        }
        res = false;
      }
    }

    return res;
  }

  if (this->IsUnion()) {
    // Make sure all children of a union have offset 0.
    for (auto &child : children) {
      if (child->GetOffsetBytes() != 0 && !child->IsPadding()) {
        if (verify_verbose) {
          LOG(ERROR) << "Union child offset not 0: " << child->GetOffsetBytes()
                     << " != 0 for " << "\n"
                     << *child << "on node: " << this->GetName() << "\n";
        }
        res = false;
      }
    }

    for (auto &child : children) {
      res &= child->Verify(this, older_sibling, verify_verbose);
    }

    return res;
  }

  // Make sure total access count is the sum of child access counts.
  if (NumChildren() > 0) {
    uint64_t total_child_access_count = 0;
    uint64_t total_child_size = 0;
    for (auto &child : children) {
      total_child_access_count += child->GetTotalAccessCount();
      total_child_size += child->GetSizeBits() * child->GetMultiplicity();
    }
    if (total_child_access_count < GetTotalAccessCount()) {
      if (verify_verbose) {
        LOG(ERROR) << "Total count mismatch: Total child access count "
                   << total_child_access_count << " < " << GetTotalAccessCount()
                   << " for : " << "\n"
                   << *this << "\n";
      }
      res = false;
    }
    if (total_child_size != GetSizeBits()) {
      if (verify_verbose) {
        LOG(ERROR) << "Size mismatch: Total child size " << total_child_size
                   << " != " << GetSizeBits() << " for : " << "\n"
                   << *this << "\n";
      }
      res = false;
    }
  }
  if ((!IsPadding() && GetTypeName().empty())) {
    if (verify_verbose) {
      LOG(ERROR) << "Not padding and empty type name for " << "\n"
                 << *this << "\n";
    }
    res = false;
  }

  // It is okay to have unresolved types, but we still want to print a message.
  if (IsUnresolvedType()) {
    if (verify_verbose) {
      LOG(ERROR) << "Unresolved type for " << "\n" << *this << "\n";
    }
  }

  if (parent != nullptr) {
    // make sure offsets are correct.
    if (GetGlobalOffsetBits() !=
        parent->GetGlobalOffsetBits() + GetOffsetBits()) {
      if (verify_verbose) {
        LOG(ERROR) << "Parent-Child Offset mismatch: " << GetGlobalOffsetBits()
                   << " != " << parent->GetGlobalOffsetBits() + GetOffsetBits()
                   << " for " << "\n"
                   << *this << "\n";
      }
      res = false;
    }
  } else {
    if ((GetGlobalOffsetBits() != 0 || GetOffsetBits() != 0)) {
      if (verify_verbose) {
        LOG(ERROR) << "Root offset not 0: " << GetGlobalOffsetBits()
                   << " != " << GetOffsetBits() << " for " << "\n"
                   << *this << "\n";
      }
    }
  }
  if (older_sibling != nullptr) {
    // Make sure we have total partial ordering.
    if (GetGlobalOffsetBits() <= older_sibling->GetGlobalOffsetBits()) {
      if (verify_verbose) {
        LOG(ERROR) << "Siblings do not have partial ordering in "
                      "global offsets "
                   << GetGlobalOffsetBits()
                   << " <= " << older_sibling->GetGlobalOffsetBits() << " for "
                   << "\n"
                   << *older_sibling << "\n"
                   << *this << "\n";
      }
      res = false;
    }
    // Make sure size and offset are consistent.
    if ((older_sibling->GetSizeBits() + older_sibling->GetOffsetBits() !=
             GetOffsetBits() ||
         older_sibling->GetGlobalOffsetBits() + older_sibling->GetSizeBits() !=
             GetGlobalOffsetBits())) {
      if (verify_verbose) {
        LOG(ERROR) << "Siblings do not have consistent size and offset "
                   << older_sibling->GetSizeBits() << " + "
                   << older_sibling->GetOffsetBits()
                   << " != " << GetOffsetBits() << " or "
                   << older_sibling->GetGlobalOffsetBits() << " + "
                   << older_sibling->GetSizeBits()
                   << " != " << GetGlobalOffsetBits() << " for " << "\n"
                   << *older_sibling << "\n"
                   << *this << "\n";
      }
      res = false;
    }
  } else {
    if (GetOffsetBits() != 0) {
      if (verify_verbose) {
        LOG(ERROR) << "First child does not have offset of 0: "
                   << GetOffsetBits() << " != 0 for " << "\n"
                   << *this << "\n";
      }
      res = false;
    }
  }
  if (GetSizeBits() <= 0) {
    if (verify_verbose) {
      LOG(ERROR) << "Size must be positive: " << GetSizeBits() << " for "
                 << "\n"
                 << *this << "\n";
    }
    res = false;
  }
  const Node *older_sibling_of_child = nullptr;
  for (auto &child : children) {
    res &= child->Verify(this, older_sibling_of_child, verify_verbose);
    older_sibling_of_child = child.get();
  }
  return res;
}

absl::Status TypeTree::Node::MergeCounts(const Node *other) {
  size_t pos = GetTypeName().find('[');
  bool has_same_type =
      GetTypeName() == other->GetTypeName() ||
      (pos != std::string::npos &&
       GetTypeName().substr(0, pos) == other->GetTypeName().substr(0, pos));
  if (GetName() != other->GetName() || NumChildren() != other->NumChildren() ||
      !has_same_type) {
    // std::cout << "Trying to merge counts for distinct trees " <<
    // GetTypeName()
    //           << " vs " << other->GetTypeName() << "\n";
    // std::cout << "global_offset: " << global_offset << " vs "
    //           << other->global_offset << "\n";
    // std::cout << "size: " << GetSizeBytes() << " vs " <<
    // other->GetSizeBytes()
    //           << "\n";
    // std::cout << "name: " << GetName() << " vs " << other->GetName() << "\n";
    // std::cout << "num_children: " << NumChildren() << " vs "
    //           << other->NumChildren() << "\n";
    // std::cout << "has_same_type: " << has_same_type << "\n";
    return absl::InvalidArgumentError(
        absl::StrCat("Trying to merge counts for distinct trees --> ",
                     GetTypeName(), " vs ", other->GetTypeName()));
  }

  access_counters.total += other->access_counters.total;
  access_counters.access += other->access_counters.access;
  access_counters.llc_miss += other->access_counters.llc_miss;
  for (int i = 0; i < NumChildren(); i++) {
    // We cheat here a bit by casting away the constness of
    // the other node. This is safe because we are only
    // modifying the access counts, not the structure of the
    // tree or the node.
    RETURN_IF_ERROR(
        const_cast<Node *>(GetChild(i))->MergeCounts(other->GetChild(i)));
  }

  return absl::OkStatus();
}

absl::Status TypeTree::MergeCounts(const TypeTree *other) {
  return root_->MergeCounts(other->Root());
}

void TypeTree::Node::CreateChildFromSuboject(
    const ObjectLayout &object_layout) {
  for (auto &subobject : object_layout.subobjects()) {
    std::unique_ptr<Node> node =
        Node::CreateNodeFromObjectLayout(subobject, this);
    node->CreateChildFromSuboject(subobject);
    AddChild(std::move(node));
  }
  this->object_layout.clear_subobjects();
}

std::unique_ptr<TypeTree> TypeTree::CreateTreeFromObjectLayout(
    const ObjectLayout &object_layout, std::string root_type_name,
    std::string container_name) {
  std::unique_ptr<Node> root =
      Node::CreateNodeFromObjectLayout(object_layout, nullptr);
  root->CreateChildFromSuboject(object_layout);
  return std::make_unique<TypeTree>(std::move(root), root_type_name,
                                    /*from_container=*/!container_name.empty(),
                                    /*container_name=*/container_name);
}

ObjectLayout TypeTree::CreateObjectLayoutFromTree(const TypeTree &type_tree) {
  ObjectLayout object_layout = type_tree.root_->GetObjectLayout();
  type_tree.root_->CreateObjectLayoutFromChildren(object_layout);
  return object_layout;
}
void TypeTree::Node::CreateObjectLayoutFromChildren(
    ObjectLayout &object_layout) const {
  for (int i = 0; i < NumChildren(); i++) {
    ObjectLayout *subobject = new ObjectLayout(GetChild(i)->GetObjectLayout());
    object_layout.mutable_subobjects()->AddAllocated(subobject);
    GetChild(i)->CreateObjectLayoutFromChildren(*subobject);
  }
}

absl::string_view TypeTree::TypeKindToString(
    ObjectLayout::Properties::TypeKind type_kind) {
  switch (type_kind) {
    case ObjectLayout::Properties::UNKNOWN_TYPE:
      return "UNKNOWN_TYPE";
    case ObjectLayout::Properties::BUILTIN_TYPE:
      return "BUILTIN_TYPE";
    case ObjectLayout::Properties::RECORD_TYPE:
      return "RECORD_TYPE";
    case ObjectLayout::Properties::INDIRECTION_TYPE:
      return "INDIRECTION_TYPE";
    case ObjectLayout::Properties::ARRAY_TYPE:
      return "ARRAY_TYPE";
    case ObjectLayout::Properties::PADDING_TYPE:
      return "PADDING_TYPE";
    case ObjectLayout::Properties::ENUM_TYPE:
      return "ENUM_TYPE";
    default:
      return "UNKNOWN_TYPE";
  }
}

ObjectLayout::Properties::TypeKind TypeTree::DwarfTypeKindToObjectTypeKind(
    DwarfMetadataFetcher::DataType data_type) {
  switch (data_type) {
    case DwarfMetadataFetcher::DataType::STRUCTURE:
    case DwarfMetadataFetcher::DataType::CLASS:
    case DwarfMetadataFetcher::DataType::UNION:
      return ObjectLayout::Properties::RECORD_TYPE;
    case DwarfMetadataFetcher::DataType::BASE_TYPE:
      return ObjectLayout::Properties::BUILTIN_TYPE;
    case DwarfMetadataFetcher::DataType::POINTER_LIKE:
      return ObjectLayout::Properties::INDIRECTION_TYPE;
    case DwarfMetadataFetcher::DataType::ENUM:
      return ObjectLayout::Properties::ENUM_TYPE;
    case DwarfMetadataFetcher::DataType::UNKNOWN:
    case DwarfMetadataFetcher::DataType::NAMESPACE:
    case DwarfMetadataFetcher::DataType::SUBPROGRAM:
    default:
      return ObjectLayout::Properties::UNKNOWN_TYPE;
  }
}

absl::StatusOr<const TypeTree::Node *> TypeTree::Node::FindNodeWithTypeName(
    absl::string_view type_name) const {
  for (const auto &child : children) {
    if (child->GetTypeName() == type_name) {
      return child.get();
    }
    auto res = child->FindNodeWithTypeName(type_name);
    if (res.ok()) {
      return res;
    }
  }
  return absl::NotFoundError(
      absl::StrCat("Merge node not found with type name: ", type_name));
}

absl::StatusOr<const TypeTree::Node *> TypeTree::FindNodeWithTypeName(
    absl::string_view type_name) const {
  return root_->FindNodeWithTypeName(type_name);
}

absl::Status TypeTree::Node::MergeTreeIntoThis(const Node *other,
                                               int64_t starting_offset) {
  for (int i = 0; i < other->NumChildren(); i++) {
    const TypeTree::Node *child = other->GetChild(i);
    std::unique_ptr<Node> child_copy = CopyNode(*child);
    RETURN_IF_ERROR(
        child_copy->MergeTreeIntoThis(other->GetChild(i), starting_offset));
    AddChild(std::move(child_copy));
  }
  return absl::OkStatus();
}

absl::Status TypeTree::MergeTreeIntoThis(const TypeTree *other) {
  if (Empty()) {
    return absl::InvalidArgumentError("This tree is empty.");
  }
  if (other == nullptr) {
    return absl::InvalidArgumentError("Other tree is null.");
  }

  ASSIGN_OR_RETURN(const Node *merge_node_const,
                   FindNodeWithTypeName(other->Name()));

  if (merge_node_const->NumChildren() != 0) {
    return absl::InvalidArgumentError(
        "Merging tree into node with children is not supported.");
  }
  // We cheat here in the name of efficiency, to avoid copying the entire
  // "this" tree. We know this is safe because the "this" tree remains alive
  // and the merged node will be held by unique_ptr inside the vector of the
  // parent of the merged node.
  Node *merge_node = const_cast<Node *>(merge_node_const);
  RETURN_IF_ERROR(merge_node->MergeTreeIntoThis(
      other->Root(), merge_node->GetGlobalOffsetBits()));
  BuildSizesBottomUp();
  InferOffsetsFromSizes();
  return absl::OkStatus();
}

void TypeTree::Node::InferOffsetsFromSizes() {
  int64_t curr_offset = 0;
  for (auto &child : children) {
    child->global_offset = global_offset + curr_offset;
    child->object_layout.mutable_properties()->set_offset_bits(curr_offset);
    curr_offset += child->GetFullSizeBits();
    child->InferOffsetsFromSizes();
  }
}

void TypeTree::Node::BuildSizesBottomUp() {
  for (auto &child : children) {
    child->BuildSizesBottomUp();
  }
  if (GetFullSizeBits() == 0) {
    int64_t size_bits = 0;
    for (auto &child : children) {
      size_bits += child->GetFullSizeBits();
    }
    SetSizeBits(size_bits);
  }
}

absl::StatusOr<std::unique_ptr<FieldAccessHistogram>>
FieldAccessHistogram::Create(const TypeTree *type_tree) {
  if (type_tree == nullptr) {
    return absl::InvalidArgumentError("Type tree is null.");
  }
  if (type_tree->Root()->GetSizeBits() < 0) {
    return absl::InvalidArgumentError("Type tree has negative size.");
  }
  auto field_access_histogram = std::make_unique<FieldAccessHistogram>(
      type_tree->Name(), type_tree->Root()->GetSizeBits());
  std::queue<const TypeTree::Node *> node_queue;
  node_queue.push(type_tree->Root());
  while (!node_queue.empty()) {
    const TypeTree::Node *node = node_queue.front();

    if (node->NumChildren() == 0) {
      std::unique_ptr<TypeTree::Node> node_copy =
          TypeTree::Node::CopyNode(*node);
      field_access_histogram->offset_to_idx_[node->GetGlobalOffsetBytes()] =
          field_access_histogram->nodes_.size();
      field_access_histogram->nodes_.push_back(std::move(node_copy));
    }
    node_queue.pop();
    for (int i = 0; i < node->NumChildren(); i++) {
      node_queue.push(node->GetChild(i));
    }
  }
  return field_access_histogram;
}

void FieldAccessHistogram::Dump(std::ostream &out) const {
  out << "FieldAccessHistogram: " << root_type_name_ << "\n";
  for (const auto &node : nodes_) {
    out << *node << "\n";
  }
}

std::ostream &operator<<(std::ostream &os, const TypeTree::Node &node) {
  os << "|" << node.NameToString(node.GetTypeName()) << " " << " "
     << node.GetGlobalOffsetBytes() << " " << node.GetSizeBytes() << "|";
  return os;
}
}  // namespace devtools_crosstool_fdo_field_access
