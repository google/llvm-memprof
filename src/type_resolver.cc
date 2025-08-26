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

#include <sys/types.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "absl/container/flat_hash_set.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/ascii.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/strings/strip.h"
#include "dwarf_metadata_fetcher.h"
#include "llvm/include/llvm/Demangle/Demangle.h"
#include "re2/re2.h"
#include "status_macros.h"
#include "type_tree.h"
#include "type_tree_container_blueprints.h"

namespace devtools_crosstool_fdo_field_access {

#define ARRAY_SIZE(a) (sizeof(a) / sizeof(a[0]))

constexpr absl::string_view kSTLContainerTypes[] = {
    "std::_Vector_base",
    "std::__u::_Vector_base",
    "std::_Deque_base",
    "std::__u::_Deque_base",
    "std::_Rb_tree",
    "std::__u::_Rb_tree",
    "std::__u::__tree",
    "std::__tree",
    "std::__detail::_Hashtable_alloc",
    "std::__u::__detail::_Hashtable_alloc",
    "std::_Fwd_list_base",
    "std::__u::_Fwd_list_base",
    "std::__cxx11::_List_base",
    "std::__u::__cxx11::list",
    "absl::FixedArray",
    "xalanc_1_10::XalanVector",
};

constexpr absl::string_view kSTLContainerLeafCheckTypes[] = {
    "std::vector",
    "std::__u::vector",
    "std::deque",
    "std::__u::deque",
    "std::set",
    "std::__u::set",
    "std::forward_list",
    "std::__u::forward_list",
    "std::__cxx11::list",
    "std::__u::__cxx11::list",
    "std::stack",
    "std::__u::stack",
    "std::queue",
    "std::__u::queue",
    "std::priority_queue",
    "std::__u::priority_queue",
    "std::map",
    "std::__u::map",
    "std::multimap",
    "std::__u::multimap",
    "std::multiset",
    "std::__u::multiset",
    "std::flat_multiset",
    "std::__u::flat_multiset",
    "std::flat_multimap",
    "std::__u::flat_multimap",
    "std::unordered_set",
    "std::__u::unordered_set",
    "std::unordered_map",
    "std::__u::unordered_map",
    "std::unordered_multiset",
    "std::__u::unordered_multiset",
    "std::unordered_multimap",
    "std::__u::unordered_multimap",
};

constexpr absl::string_view kSmartPointersTypes[] = {
    "_ZSt11make_unique", "_ZSt11make_shared", "_ZNSt3__u15allocate_shared",
    "_ZNSt3__u11make_unique"};

constexpr absl::string_view kADTContainerTypes[] = {
    "llvm::SmallVectorTemplateBase<", "llvm::PagedVector<",
    "llvm::SmallPtrSetImpl<", "llvm::StringMap<",
    "llvm::ImutAVLFactory<, absl::inlined_vector_internal:"};

constexpr absl::string_view kADTDenseContainerTypes[] = {"llvm::DenseMapBase"};

constexpr absl::string_view kCharContainerTypesLeafFrame[] = {
    "std::__cxx11::basic_string", "std::basic_string",
    "absl::cord_internal::", "std::__u::basic_string", "absl::Cord::"};

constexpr absl::string_view kABSLContainerSwissMapTypes[] = {
    "absl::container_internal::raw_hash_map<",
    "absl::container_internal::raw_hash_set<",
};

// constexpr absl::string_view kABSLContainerNodeHashTypes[] = {
//     "absl::container_internal::NodeHashMapPolicy",
//     "absl::container_internal::NodeHashSetPolicy",
// };

constexpr absl::string_view kABSLContainerFlatHashTypes[] = {
    "absl::container_internal::FlatHashMapPolicy",
    "absl::container_internal::FlatHashSetPolicy",
};

constexpr absl::string_view kABSLContainerBtreeTypes[] = {
    "absl::container_internal::btree<",
};

constexpr absl::string_view kSpecialAllocatingFunctions[] = {
    "std::get_temporary_buffer",
    "std::__u::get_temporary_buffer",
};

constexpr absl::string_view kAllocatorWrappers[] = {
    "std::allocator", "std::__u::allocator", "std::__new_allocator",
    "muppet::instant::PolymorphicAllocator",
    "xalanc_1_10::MemoryManagedConstructionTraits"};

// Keywords for functions specially inserted by memprof. Used to distinguish
// user types allocated by the container vs the metadata.
constexpr absl::string_view kMemprofInsertedFunctions[] = {
    "__memprof_ctrl_alloc",
};

static std::string stripTrailingColons(const std::string& str) {
  size_t last_non_colon = str.find_last_not_of(':');
  if (last_non_colon == std::string::npos) {
    return "";
  } else {
    return str.substr(0, last_non_colon + 1);
  }
}

std::optional<const std::string> StartsWithAnyOf(
    absl::string_view str, const absl::string_view keywords[], size_t N) {
  for (int i = 0; i < N; ++i) {
    if (absl::StartsWith(str, keywords[i])) {
      return std::string(keywords[i]);
    }
  }
  return std::nullopt;
}

std::string BuildCallstackString(
    const DwarfTypeResolver::CallStack& callstack) {
  std::string callstack_string;
  for (const auto& frame : callstack) {
    absl::StrAppend(&callstack_string, frame.function_name,
                    " l:", frame.line_offset, " c:", frame.column, "\n");
  }
  return callstack_string;
}

std::string BuildErrorMessageInResolution(
    const std::vector<std::string>& formal_params,
    const DwarfTypeResolver::CallStack& callstack,
    DwarfTypeResolver::ContainerResolutionStrategy strategy,
    absl::string_view extra_info = "") {
  std::string error_message =
      absl::StrCat("Type resolution strategy failed: ",
                   DwarfTypeResolver::ContainerResolutionStrategy::TypeToString(
                       strategy.container_type),
                   " for container: ", strategy.container_name,
                   " with container class name: ", strategy.lookup_type,
                   " with formal params: ");
  for (const auto& param : formal_params) {
    absl::StrAppend(&error_message, param, " ");
  }
  absl::StrAppend(&error_message, " at callstack: \n",
                  BuildCallstackString(callstack));
  if (!extra_info.empty()) {
    absl::StrAppend(&error_message, "\n", extra_info);
  }
  return error_message;
}

// Check if there is a conflict in a field offset. This can happen in
// some STL cases, such as std::pair or std::vector. CPP uses for template
// types. Pragmatic heuristic we use: Resolve all types with the same
// offset, and take the one with the largest size. Often the "hidden" type has
// a size of 1 byte, even when the "real" field has a larger size.
absl::StatusOr<std::vector<const DwarfMetadataFetcher::FieldData*>>
DwarfTypeResolver::ResolveFieldConflicts(
    const DwarfMetadataFetcher::TypeData* type_data) {
  std::vector<const DwarfMetadataFetcher::FieldData*> resolved_fields;

  // If we have a union, we don't need to resolve fields --- We expect
  // conflicts! We should ONLY have legal conflicts in unions.
  if (type_data->data_type == DwarfMetadataFetcher::DataType::UNION) {
    resolved_fields.reserve(type_data->fields.size());
    for (auto& field : type_data->fields) {
      resolved_fields.push_back(field.get());
    }
    return resolved_fields;
  }

  absl::flat_hash_set<size_t> offsets;
  for (auto& field : type_data->fields) {
    offsets.insert(field->offset);
  }
  std::vector<size_t> sorted_offsets(offsets.begin(), offsets.end());
  std::sort(sorted_offsets.begin(), sorted_offsets.end());

  for (size_t offset : sorted_offsets) {
    const DwarfMetadataFetcher::TypeData* type_data_for_offset = nullptr;
    const DwarfMetadataFetcher::FieldData* field_data_for_offset = nullptr;

    // Get the offset to index map in the typedata. We detect a conflict
    // using this map.
    auto it = type_data->offset_idx.find(offset);
    if (it == type_data->offset_idx.end()) {
      return absl::InvalidArgumentError(
          absl::StrCat("Dwarf data is invalid, field offset index and "
                       "field data invalid "
                       "for type: ",
                       type_data->name));
    }

    // Normal case, there is no conflict.
    if (it->second.size() == 1) {
      resolved_fields.push_back(type_data->fields[*(it->second.begin())].get());
      continue;
    }

    absl::StatusOr<const DwarfMetadataFetcher::TypeData*> status_or_type_data;
    for (const auto& idx : it->second) {
      const DwarfMetadataFetcher::FieldData* field_data =
          type_data->fields[idx].get();

      if (!field_data) {
        return absl::InternalError(
            absl::StrCat("Field data is null for type: ", type_data->name,
                         " at offset: ", offset));
      }

      status_or_type_data = metadata_fetcher_->GetType(field_data->type_name);

      // Continue if we can't resolve the type.
      if (!status_or_type_data.ok()) {
        continue;
      }

      // Set initial largest field.
      if (!type_data_for_offset || !field_data_for_offset) {
        type_data_for_offset = status_or_type_data.value();
        field_data_for_offset = field_data;
        continue;
      }
      // Special case when conflicting fields have same size and same number of
      // fields
      if (type_data_for_offset->size == status_or_type_data.value()->size &&
          type_data_for_offset->fields.size() ==
              status_or_type_data.value()->fields.size()) {
        // Tiebreaker: When both options are the same size, if the new
        // type is inherited, and the old type is not, replace the old
        // type.
        if (!field_data_for_offset->inherited && field_data->inherited) {
          type_data_for_offset = status_or_type_data.value();
          field_data_for_offset = field_data;
        } else if (absl::StartsWith(field_data_for_offset->name, "_") &&
                   !absl::StartsWith(field_data->name, "_")) {
          // Tiebreaker: When both options are the same size and both have
          // the same inheritance state, look for "_" prefix.
          type_data_for_offset = status_or_type_data.value();
          field_data_for_offset = field_data;
        } else if (field_data_for_offset->inherited == field_data->inherited &&
                   !(absl::StartsWith(field_data_for_offset->name, "_"))) {
          // If both types have the same size, the same inheritance state,
          // and both have the same "_" prefix, we have a true conflict.
          // In this case, for now we do not care which type we choose.
          LOG(WARNING) << absl::StrCat(
              "Multiple types with same size, number of fields and tag for "
              "offset "
              "confict: ",
              offset, " for type: ", type_data->name, ". \n",
              "Conficting types: \n", field_data_for_offset->type_name, "/",
              type_data_for_offset->size, "/",
              type_data_for_offset->fields.size(), "/",
              field_data_for_offset->inherited, "\n == \n",
              field_data->type_name, "/", status_or_type_data.value()->size,
              "/", status_or_type_data.value()->fields.size(), "/",
              field_data->inherited);
        }
        continue;
      }

      // Normal conflict resolution: replace smaller field with larger
      // field.
      if (type_data_for_offset->size < status_or_type_data.value()->size) {
        type_data_for_offset = status_or_type_data.value();
        field_data_for_offset = field_data;
        continue;
      }
      // Secondary conflict resolution: field with if the type of the field has
      // more fields.
      if (type_data_for_offset->fields.size() <
          status_or_type_data.value()->fields.size()) {
        type_data_for_offset = status_or_type_data.value();
        field_data_for_offset = field_data;
      }
    }

    if (!type_data_for_offset || !field_data_for_offset) {
      return std::vector<const DwarfMetadataFetcher::FieldData*>();
    }

    // Once we have resolved the type with the largest size, we can
    // add it to the resolved_fields vector.
    resolved_fields.push_back(field_data_for_offset);
  }

  // Sanity check to make sure the number of resolved fields is the same
  // as the number of unique offsets. in the original type.
  if (resolved_fields.size() != type_data->offset_idx.size()) {
    return absl::InternalError(absl::StrCat(
        "Panic! Resolve field conflicts was not able to resolve "
        "all fields for type: ",
        type_data->name, ". Size after resolve: ", resolved_fields.size(),
        " vs original size: ", type_data->offset_idx.size(),
        " Size before resolve: ", type_data->fields.size(),
        " Should be size: ", type_data->offset_idx.size()));
  }
  return resolved_fields;
}

// If ends with "*" is a pointer, if ends with "&" is a reference, and if ends
// with "() or )>" is a function. All are indirection types with size
// `pointer_size`.
bool DwarfTypeResolver::IsIndirection(absl::string_view type_name) {
  return absl::EndsWith(type_name, "*") || absl::EndsWith(type_name, "&") ||
         absl::EndsWith(type_name, "()") || absl::EndsWith(type_name, ")>");
}

int64_t DwarfTypeResolver::GetArrayMultiplicity(absl::string_view type_name) {
  int64_t multiplicity = 1;
  RE2::PartialMatch(type_name, "\\[(\\d+)\\]$", &multiplicity);
  return multiplicity;
}

std::string DwarfTypeResolver::GetArrayChildTypeName(
    absl::string_view type_name) {
  std::string child_type_name = std::string(type_name);
  RE2::GlobalReplace(&child_type_name, "\\[(\\d+)\\]$", "");
  return child_type_name;
}
void DwarfTypeResolver::DereferencePointer(std::string* type_name) {
  // Remove exactly one space and one "*" character in typename.
  RE2::GlobalReplace(type_name, " \\*$", "");
}

void DwarfTypeResolver::CleanTypeName(std::string* type_name) {
  // Remove whitespace from pointer. This is so we have a unified
  // way of handling pointers here, so "A*" instead of "A *", which can
  // otherwise cause confusion. Only do this to pointer at end.
  RE2::GlobalReplace(type_name, " \\*$", "*");

  // The keyword const is not in the Dwarf type name, so we need to remove it.
  // Sometimes, types have DW_tag_const_type, but a lot of times we can not
  // rely on this type being generated. It is safer to remove const, as it is
  // not important for type resolution.
  *type_name = absl::StripPrefix(*type_name, "const");

  // Strip any leading whitespace leftover from stripping const or consuming
  // brackets.
  *type_name = absl::StripLeadingAsciiWhitespace(*type_name);
}

std::string DwarfTypeResolver::UnwrapAndCleanTypeName(
    absl::string_view type_name) {
  std::string alloc_type = DwarfMetadataFetcher::ConsumeAngleBracket(
      type_name.begin(), type_name.end());
  DwarfTypeResolver::CleanTypeName(&alloc_type);

  // Very hacky way of dealing with Polymorphic allocator type.
  if (alloc_type.ends_with(", false")) {
    alloc_type = alloc_type.substr(0, alloc_type.size() - 7);
  } else if (alloc_type.ends_with(", true")) {
    alloc_type = alloc_type.substr(0, alloc_type.size() - 6);
  }
  return alloc_type;
}

std::string WrapType(absl::string_view outer_type,
                     absl::string_view inner_type) {
  std::string wrapped_type = absl::StrCat(
      outer_type, "<", inner_type, inner_type.ends_with(">") ? " >" : ">");
  return wrapped_type;
}

absl::StatusOr<std::unique_ptr<TypeTree::Node>> DwarfTypeResolver::BuildTree(
    absl::string_view type_name) {
  if (IsIndirection(type_name)) {
    return TypeTree::Node::CreatePointerNode(
        type_name, type_name, /*offset_bits=*/0, /*multiplicity=*/1,
        metadata_fetcher_->GetPointerSize() * 8,
        /*parent_node=*/nullptr);
  }

  ASSIGN_OR_RETURN(const DwarfMetadataFetcher::TypeData* type_data,
                   metadata_fetcher_->GetType(type_name));
  std::unique_ptr<TypeTree::Node> root_node =
      TypeTree::Node::CreateRootNode(type_name, type_data);

  // auto type_data_or_err =  metadata_fetcher_->GetType(type_name);
  // if(!type_data_or_err.ok()){
  //   return type_data_or_err.status();
  // }
  // const DwarfMetadataFetcher::TypeData* type_data = type_data_or_err.value();

  ASSIGN_OR_RETURN(
      const std::vector<const DwarfMetadataFetcher::FieldData*> resolved_fields,
      ResolveFieldConflicts(type_data));

  uint32_t field_index = 0;
  for (auto field_data : resolved_fields) {
    std::unique_ptr<TypeTree::Node> child_node = BuildTreeRecursive({
        .type_name = field_data->type_name,
        .field_name = field_data->name,
        .field_index = field_index,
        .field_offset = field_data->offset * 8,
        .multiplicity = 1,
        .parent_node = root_node.get(),
        .resolved_fields = resolved_fields,
    });
    QCHECK(child_node != nullptr)
        << "Child node is null for type: " << type_name
        << " at offset: " << field_data->offset;
    root_node->AddChildAndInsertPaddingIfNecessary(
        std::move(child_node), root_node.get(), field_index, resolved_fields);
    field_index++;
  }
  return root_node;
}

std::unique_ptr<TypeTree::Node> DwarfTypeResolver::BuildTreeRecursive(
    BuilderCtxt ctxt) {
  QCHECK(ctxt.parent_node != nullptr) << "Parent can't be null.";

  // Indirection case: we create a node manually without getting the base type
  // of the indirection based on the pointer size.
  if (IsIndirection(ctxt.type_name)) {
    return TypeTree::Node::CreatePointerNode(
        ctxt.field_name, ctxt.type_name, ctxt.field_offset, ctxt.multiplicity,
        metadata_fetcher_->GetPointerSize() * 8, ctxt.parent_node);
  }
  int64_t child_multiplicity = GetArrayMultiplicity(ctxt.type_name);

  if (child_multiplicity > 1 /*Array case.*/) {
    // An array type node is created with the size of the all array elements
    // summed up. An array node will always have exactly one child, which is
    // the type of the array elements. The multiplicity of the child is the
    // number of elements in the array.

    // Create node without size for now, since cannot get the size until
    // the whole subtree is resolved.
    std::unique_ptr<TypeTree::Node> curr_node =
        TypeTree::Node::CreateArrayTypeNode(
            ctxt.field_name, ctxt.type_name, /*size_bits=*/-1,
            ctxt.field_offset, ctxt.multiplicity, ctxt.parent_node);

    std::unique_ptr<TypeTree::Node> subtree = BuildTreeRecursive({
        .type_name = GetArrayChildTypeName(ctxt.type_name),
        .field_name = "[_]",
        .field_index = 0,
        .field_offset = 0,
        .multiplicity = child_multiplicity,
        .parent_node = curr_node.get(),
        .resolved_fields = {},
    });

    // Once all the subtree is resolved, we can set the size of the array.
    // The only scenario in which this breaks, is if we have an are forced to
    // create an Unresolved node or padding in the subtree, which may rely in
    // the parent size. This should be extremely rare, if at all possible.
    curr_node->SetSizeBits(subtree->GetSizeBits() * subtree->GetMultiplicity());
    curr_node->AddChildAndInsertPaddingIfNecessary(
        std::move(subtree), curr_node.get(), /*field_index=*/0, {});
    return curr_node;
  }

  // Normal case: we create a node based on the TypeData from the
  // DwarfMetadataFetcher.
  auto status_or = metadata_fetcher_->GetType(ctxt.type_name);
  if (!status_or.ok()) {
    int64_t inferred_size =
        ctxt.resolved_fields.empty() ? ctxt.parent_node->GetSizeBits()
        : ctxt.field_index >= ctxt.resolved_fields.size() - 1
            ? ctxt.parent_node->GetSizeBits() -
                  ctxt.resolved_fields[ctxt.field_index]->offset * 8
            : ctxt.resolved_fields[ctxt.field_index + 1]->offset * 8 -
                  ctxt.resolved_fields[ctxt.field_index]->offset * 8;
    return TypeTree::Node::CreateUnresolvedTypeNode(
        ctxt.field_name, ctxt.type_name, ctxt.field_offset, ctxt.multiplicity,
        inferred_size, ctxt.parent_node);
  }

  const DwarfMetadataFetcher::TypeData* type_data = status_or.value();
  std::unique_ptr<TypeTree::Node> curr_node =
      TypeTree::Node::CreateNodeFromTypedata(
          ctxt.field_name, ctxt.type_name, ctxt.field_offset, ctxt.multiplicity,
          type_data, ctxt.parent_node);

  absl::StatusOr<std::vector<const DwarfMetadataFetcher::FieldData*>>
      resolved_fields_or = ResolveFieldConflicts(type_data);
  if (!status_or.ok()) {
    LOG(WARNING) << resolved_fields_or.status() << "\n";
    return curr_node;
  }
  if (resolved_fields_or.value().empty()) {
    return curr_node;
  }

  const std::vector<const DwarfMetadataFetcher::FieldData*>& resolved_fields =
      resolved_fields_or.value();

  uint32_t field_index = 0;
  for (const auto& field_data : resolved_fields) {
    std::unique_ptr<TypeTree::Node> subtree = BuildTreeRecursive({
        .type_name = field_data->type_name,
        .field_name = field_data->name,
        .field_index = field_index,
        .field_offset = field_data->offset * 8,
        .multiplicity = 1,
        .parent_node = curr_node.get(),
        .resolved_fields = resolved_fields,
    });
    curr_node->AddChildAndInsertPaddingIfNecessary(
        std::move(subtree), curr_node.get(), field_index, resolved_fields);
    field_index++;
  }
  return curr_node;
}

absl::StatusOr<std::unique_ptr<TypeTree>>
DwarfTypeResolver::CreateTreeFromDwarf(absl::string_view type_name,
                                       bool from_container,
                                       absl::string_view container_name) {
  ASSIGN_OR_RETURN(std::unique_ptr<TypeTree::Node> root, BuildTree(type_name));
  return std::make_unique<TypeTree>(std::move(root), type_name,
                                    /*from_container=*/from_container,
                                    /*container_name=*/container_name);
}

absl::StatusOr<std::unique_ptr<TypeTree>>
DwarfTypeResolver::ResolveTypeFromTypeName(absl::string_view type_name) {
  return CreateTreeFromDwarf(type_name);
}

// Abseil metadata is allocated separate from user data when using memprof.
// This function checks if a callstack contains a memprof inserted function to
// mark that this allocation is metadata.
std::optional<std::string> CallStackContainsMemprof(
    const AbstractTypeResolver::CallStack& callstack) {
  for (const auto& frame : callstack) {
    for (absl::string_view memprof : kMemprofInsertedFunctions) {
      if (absl::StrContains(frame.function_name, memprof)) {
        return frame.function_name;
      }
    }
  }
  return std::nullopt;
}

absl::StatusOr<DwarfTypeResolver::ContainerResolutionStrategy>
DwarfTypeResolver::GetCallStackContainerResolutionStrategy(
    const CallStack& callstack) {
  ContainerResolutionStrategy fallthrough_strategy;

  bool has_seen_alloc = false;
  bool last_frame_has_allocator_formal_param = true;

  if (callstack.empty()) {
    return absl::InvalidArgumentError("Empty callstack.");
  }

  if (auto memprof_func_name = CallStackContainsMemprof(callstack)) {
    return ContainerResolutionStrategy(
        "__memprof::abseil_container_internal::raw_hash_set",
        *memprof_func_name,
        ContainerResolutionStrategy::kAbseilContainerInserted);
  }

  bool is_leaf = true;
  for (const auto& frame : callstack) {
    const std::string& func_name = frame.function_name;
    if (func_name.empty()) {
      return absl::InvalidArgumentError("Empty function name in callstack.");
    }

    if (const auto smart_ptr_type = StartsWithAnyOf(
            func_name, kSmartPointersTypes, ARRAY_SIZE(kSmartPointersTypes))) {
      return ContainerResolutionStrategy(
          *smart_ptr_type, func_name,
          ContainerResolutionStrategy::kSpecialAllocatingFunction);
    }

    auto status_or_formal_params =
        metadata_fetcher_->GetFormalParameters(func_name);
    if (!status_or_formal_params.ok()) {
      continue;
    }
    const std::vector<std::string>& formal_params =
        status_or_formal_params.value();

    char* demangled_name_no_params_char =
        llvm::itaniumDemangle(func_name, /*ParseParams*/ false);
    if (demangled_name_no_params_char != nullptr) {
      std::string demangled_name_no_params(demangled_name_no_params_char);
      free(demangled_name_no_params_char);
      if (auto special_allocating_function = StartsWithAnyOf(
              demangled_name_no_params, kSpecialAllocatingFunctions,
              ARRAY_SIZE(kSpecialAllocatingFunctions))) {
        return ContainerResolutionStrategy(
            *special_allocating_function, func_name,
            ContainerResolutionStrategy::kSpecialAllocatingFunction);
      }

      if (const auto container_name = StartsWithAnyOf(
              demangled_name_no_params, kCharContainerTypesLeafFrame,
              ARRAY_SIZE(kCharContainerTypesLeafFrame))) {
        return ContainerResolutionStrategy(
            stripTrailingColons(*container_name), func_name,
            ContainerResolutionStrategy::kCharContainer);
      }
    }

    last_frame_has_allocator_formal_param = false;
    // Check if the function is in the list of supported containers.
    for (const absl::string_view formal_param_dirty : formal_params) {
      // Make sure unnecessary qualifiers do not pollute the type name we are
      // looking for.
      std::string formal_param(formal_param_dirty);
      formal_param = absl::StripPrefix(formal_param, "const");
      formal_param = absl::StripLeadingAsciiWhitespace(formal_param);

      // Cleaned formal parameter prepared for output.
      std::string cleaned_formal_param(formal_param);
      DereferencePointer(&cleaned_formal_param);
      CleanTypeName(&cleaned_formal_param);

      if (const auto allocator_type =
              StartsWithAnyOf(formal_param, kAllocatorWrappers,
                              ARRAY_SIZE(kAllocatorWrappers))) {
        if (!has_seen_alloc &&
            absl::StartsWith(formal_param, *allocator_type)) {
          std::string type_name = UnwrapAndCleanTypeName(formal_param);

          fallthrough_strategy.container_type =
              ContainerResolutionStrategy::kDefaultStrategy;
          fallthrough_strategy.func_name = func_name;
          fallthrough_strategy.container_name = "unknown";
          fallthrough_strategy.lookup_type = type_name;
          // Do not return the fallthrough strategy yet, we may find a more
          // specific strategy later in the callstack.
        }
      }

      if (is_leaf) {
        if (const auto container_type =
                StartsWithAnyOf(formal_param, kSTLContainerLeafCheckTypes,
                                ARRAY_SIZE(kSTLContainerLeafCheckTypes))) {
          return ContainerResolutionStrategy(
              *container_type, frame.function_name,
              ContainerResolutionStrategy::kLeafContainerGWPStrategy,
              formal_param);
        }
      }

      if (const auto container_type =
              StartsWithAnyOf(formal_param, kSTLContainerTypes,
                              ARRAY_SIZE(kSTLContainerTypes))) {
        return ContainerResolutionStrategy(
            *container_type, callstack.at(0).function_name,
            ContainerResolutionStrategy::kAllocatorAllocate);
      }

      if (const auto container_type =
              StartsWithAnyOf(formal_param, kADTContainerTypes,
                              ARRAY_SIZE(kADTContainerTypes))) {
        return ContainerResolutionStrategy(
            container_type->substr(0, container_type->length() - 1), func_name,
            ContainerResolutionStrategy::kADTContainer, cleaned_formal_param);
      }
      if (const auto container_type =
              StartsWithAnyOf(formal_param, kADTDenseContainerTypes,
                              ARRAY_SIZE(kADTDenseContainerTypes))) {
        return ContainerResolutionStrategy(
            *container_type, func_name,
            ContainerResolutionStrategy::kADTDenseContainer,
            cleaned_formal_param);
      }

      if (const auto container_type =
              StartsWithAnyOf(formal_param, kABSLContainerSwissMapTypes,
                              ARRAY_SIZE(kABSLContainerSwissMapTypes))) {
        // In some special cases node_hash_set uses normal allocator type. Then
        // we can just use STL container strategy.
        absl::StatusOr<int64_t> alignment =
            GetAlignmentFromAbslAllocatorCall(callstack.at(0).function_name);
        if (!alignment.ok()) {
          alignment = 64;
          /* ======== HARCODED ABSL CONTAINER VALUES for now ======== */
          // return ContainerResolutionStrategy(
          //     container_type->substr(0, container_type->length() - 1),
          //     callstack.at(0).function_name,
          //     ContainerResolutionStrategy::kAbslAllocatorAllocate,
          //     cleaned_formal_param);
          /* ======== END HARCODED ABSL CONTAINER VALUES for now ======== */
        }

        absl::StatusOr<const DwarfMetadataFetcher::TypeData*>
            hash_set_typedata_status = metadata_fetcher_->GetType(formal_param);
        if (!hash_set_typedata_status.ok()) {
          return ContainerResolutionStrategy(
              container_type->substr(0, container_type->length() - 1),
              callstack.at(0).function_name,
              ContainerResolutionStrategy::kAbslAllocatorAllocate,
              cleaned_formal_param);
        }
        const DwarfMetadataFetcher::TypeData* hash_set_typedata =
            hash_set_typedata_status.value();

        if (hash_set_typedata->formal_parameters.empty()) {
          return absl::NotFoundError(
              "No formal parameters found for the hash set type.");
        }

        const std::string& policy_param =
            hash_set_typedata->formal_parameters[0];
        if (const auto policy =
                StartsWithAnyOf(policy_param, kABSLContainerFlatHashTypes,
                                ARRAY_SIZE(kABSLContainerFlatHashTypes))) {
          return ContainerResolutionStrategy(
              container_type->substr(0, container_type->length() - 1),
              func_name,
              ContainerResolutionStrategy::kAbseilContainerSwissMapFlatHash,
              cleaned_formal_param);
        } else {
          return ContainerResolutionStrategy(
              container_type->substr(0, container_type->length() - 1),
              func_name,
              ContainerResolutionStrategy::kAbseilContainerSwissMapNodeHash,
              cleaned_formal_param);
        }
      }
      if (const auto container_type =
              StartsWithAnyOf(formal_param, kABSLContainerBtreeTypes,
                              ARRAY_SIZE(kABSLContainerBtreeTypes))) {
        return ContainerResolutionStrategy(
            container_type->substr(0, container_type->length() - 1), func_name,
            ContainerResolutionStrategy::kAbseilContainerBtree,
            cleaned_formal_param);
      }

      for (const absl::string_view allocator_type : kAllocatorWrappers) {
        if (absl::StartsWith(formal_param, allocator_type) ||
            absl::StartsWith(formal_param, "absl::container_internal::")) {
          last_frame_has_allocator_formal_param = true;
          has_seen_alloc = true;
        }
      }
      is_leaf = false;
    }
  }

  if (fallthrough_strategy.lookup_type.empty()) {
    return absl::NotFoundError(absl::StrCat(
        "No heap alloc or container resolution strategy found in callstack:",
        BuildCallstackString(callstack)));
  }

  // In the case were we cannot find a specific hardcoded strategy, we go back
  // to the default strategy. This assume the leaf function name
  return fallthrough_strategy;
}

absl::StatusOr<int64_t> DwarfTypeResolver::GetAlignmentFromAbslAllocatorCall(
    absl::string_view function_name) {
  ASSIGN_OR_RETURN(std::vector<std::string> formal_params,
                   metadata_fetcher_->GetFormalParameters(function_name));
  if (formal_params.empty()) {
    return absl::NotFoundError(
        "No formal parameters found for the allocator call.");
  }
  DereferencePointer(&formal_params[0]);
  ASSIGN_OR_RETURN(const DwarfMetadataFetcher::TypeData* allocator_type_data,
                   metadata_fetcher_->GetType(formal_params[0]));
  formal_params = allocator_type_data->formal_parameters;
  if (formal_params.empty()) {
    return absl::NotFoundError(
        "No formal parameters found for the allocator call.");
  }
  DereferencePointer(&formal_params[0]);
  ASSIGN_OR_RETURN(allocator_type_data,
                   metadata_fetcher_->GetType(formal_params[0]));
  auto alignment_it = allocator_type_data->constant_variables.find("Alignment");
  if (alignment_it == allocator_type_data->constant_variables.end()) {
    return absl::NotFoundError(
        "No constant variable `Alignment` found in Absl allocator call.");
  }
  return alignment_it->second * 8;
}

absl::StatusOr<std::unique_ptr<TypeTree>>
DwarfTypeResolver::ResolveTypeFromResolutionStrategy(
    const ContainerResolutionStrategy& resolution_strategy,
    const CallStack& callstack, int64_t request_size) {
  ASSIGN_OR_RETURN(
      std::vector<std::string> formal_params,
      metadata_fetcher_->GetFormalParameters(resolution_strategy.func_name));
  switch (resolution_strategy.container_type) {
    case ContainerResolutionStrategy::kDefaultStrategy: {
      return CreateTreeFromDwarf(resolution_strategy.lookup_type,
                                 /*from_container=*/true,
                                 resolution_strategy.container_name);
    }

    case ContainerResolutionStrategy::kSpecialAllocatingFunction: {
      std::string type_name = formal_params[0];
      CleanTypeName(&type_name);
      return CreateTreeFromDwarf(type_name, /*from_container=*/true,
                                 resolution_strategy.container_name);
    }
    case ContainerResolutionStrategy::kCharContainer: {
      return CreateTreeFromDwarf("char", /*from_container=*/true,
                                 resolution_strategy.container_name);
    }
    case ContainerResolutionStrategy::kAbslAllocatorAllocate:
    case ContainerResolutionStrategy::kAllocatorAllocate: {
      // Walk the callstack from the bottom and find the lowest allocator
      // type.
      for (const auto& frame : callstack) {
        ASSIGN_OR_RETURN(formal_params, metadata_fetcher_->GetFormalParameters(
                                            frame.function_name));
        for (const absl::string_view formal_param : formal_params) {
          for (const absl::string_view allocator_type : kAllocatorWrappers) {
            if (absl::StartsWith(formal_param, allocator_type)) {
              std::string type_name = UnwrapAndCleanTypeName(formal_param);
              return CreateTreeFromDwarf(type_name, /*from_container=*/true,
                                         resolution_strategy.container_name);
            }
          }
        }
      }
      return absl::NotFoundError(BuildErrorMessageInResolution(
          formal_params, callstack, resolution_strategy,
          "There should be formal param with an allocator type."));
    }

    case ContainerResolutionStrategy::kLeafContainerGWPStrategy: {
      ASSIGN_OR_RETURN(
          const DwarfMetadataFetcher::TypeData* container_type_data,
          metadata_fetcher_->GetType(resolution_strategy.lookup_type));
      for (const auto& formal_param : container_type_data->formal_parameters) {
        if (StartsWithAnyOf(formal_param, kAllocatorWrappers,
                            ARRAY_SIZE(kAllocatorWrappers))) {
          return CreateTreeFromDwarf(UnwrapAndCleanTypeName(formal_param),
                                     /*from_container=*/true,
                                     resolution_strategy.container_name);
        }
      }
      return absl::NotFoundError(BuildErrorMessageInResolution(
          formal_params, callstack, resolution_strategy,
          "No formal parameters found for the container class."));
    }
    case ContainerResolutionStrategy::kADTContainer: {
      ASSIGN_OR_RETURN(
          const DwarfMetadataFetcher::TypeData* type_data,
          metadata_fetcher_->GetType(resolution_strategy.lookup_type));
      if (type_data->formal_parameters.empty()) {
        return absl::NotFoundError(BuildErrorMessageInResolution(
            formal_params, callstack, resolution_strategy,
            "No formal parameters found for the container class."));
      }
      return CreateTreeFromDwarf(type_data->formal_parameters[0],
                                 /*from_container=*/true,
                                 resolution_strategy.container_name);
    }

    case ContainerResolutionStrategy::kADTDenseContainer: {
      ASSIGN_OR_RETURN(
          const DwarfMetadataFetcher::TypeData* type_data,
          metadata_fetcher_->GetType(resolution_strategy.lookup_type));
      if (type_data->formal_parameters.size() < 5) {
        return absl::NotFoundError(BuildErrorMessageInResolution(
            formal_params, callstack, resolution_strategy));
      }
      return CreateTreeFromDwarf(type_data->formal_parameters[4],
                                 /*from_container=*/true,
                                 resolution_strategy.container_name);
    }
    case ContainerResolutionStrategy::kAbseilContainerSwissMapNodeHash:
    case ContainerResolutionStrategy::kAbseilContainerSwissMapFlatHash: {
      // The Swissmap type resolution relies on a typetree template to resolve
      // all the metadata allocated alongside the client type. The full type
      // resolution entails the following steps:
      // 1. Get the `Alignment` constant of the type from the allocator call.
      // 2. Get the `kWidth` constant from the `Group` class.
      // 3. Get the size of the `size_t` type.
      // 4. Get the size of a pointer type.
      // 5. Get the client type and build the type tree.
      // 6. Build the type tree template for the `BackingArray` struct from all
      // the evaluated constant, request size, and the client type information.
      // 7. Merge the client type tree into the template type tree.
      // ASSIGN_OR_RETURN(int64_t Alignment, GetAlignmentFromAbslAllocatorCall(
      //                                         callstack.at(0).function_name));
      int64_t Alignment = 8;

      /* ======== HARCODED ABSL CONTAINER VALUES for now ======== */
      // absl::StatusOr<const DwarfMetadataFetcher::TypeData*>
      // group_type_data_or =
      //     metadata_fetcher_->GetType("absl::container_internal::Group");
      // if (!group_type_data_or.ok()) {
      //   std::cout << "Group not found\n";
      //   return absl::NotFoundError(BuildErrorMessageInResolution(
      //       formal_params, callstack, resolution_strategy,
      //       "Group type not found."));
      // } else {
      //   std::cout << "Group found\n";
      // }
      // const DwarfMetadataFetcher::TypeData* group_type_data =
      //     group_type_data_or.value();
      // // ASSIGN_OR_RETURN(
      // //     const DwarfMetadataFetcher::TypeData* group_type_data,
      // //     metadata_fetcher_->GetType("absl::container_internal::Group"));
      // std::cout << "After group_type_data\n";

      // const auto it = group_type_data->constant_variables.find("kWidth");
      // if (it == group_type_data->constant_variables.end()) {
      //   std::cout << BuildErrorMessageInResolution(
      //       formal_params, callstack, resolution_strategy,
      //       "No constant variable kWidth found.");
      //   return absl::NotFoundError(BuildErrorMessageInResolution(
      //       formal_params, callstack, resolution_strategy,
      //       "No constant variable kWidth found."));
      // }
      // int64_t kWidth = it->second;
      /* ======== HARCODED ABSL CONTAINER VALUES for now ======== */
      int64_t kWidth = 16;
      /* ======== HARCODED ABSL CONTAINER VALUES for now ======== */
      // ASSIGN_OR_RETURN(const DwarfMetadataFetcher::TypeData* size_type_data,
      //                  metadata_fetcher_->GetType("size_t"));
      // int64_t size_t_size = size_type_data->size * 8;
      /* ======== HARCODED ABSL CONTAINER VALUES for now ======== */
      int64_t size_t_size = 64;

      // For now we assume that hashtablez is not enabled. When an allocation is
      // chosen for sampling, and the BackingArray has a hashtablez_info_handle,
      // this can cause the type tree to be incorrect and giving is distorted
      // field access counts. For now, there is no way to know if hashtablez is
      // enabled or from Dwarf data. Perhaps we can propose removing this field
      // from the actual heap allocation again.
      bool hashtablez_info = false;
      int64_t hashtablez_info_handle_size =
          metadata_fetcher_->GetPointerSize() * 8;

      ASSIGN_OR_RETURN(
          const DwarfMetadataFetcher::TypeData* type_data,
          metadata_fetcher_->GetType(resolution_strategy.lookup_type));

      for (const absl::string_view formal_param :
           type_data->formal_parameters) {
        for (const absl::string_view allocator_type : kAllocatorWrappers) {
          if (absl::StartsWith(formal_param, allocator_type)) {
            std::string type_name = UnwrapAndCleanTypeName(formal_param);
            if (resolution_strategy.container_type ==
                ContainerResolutionStrategy::kAbseilContainerSwissMapNodeHash) {
              absl::StrAppend(&type_name, "*");
            }
            ASSIGN_OR_RETURN(
                std::unique_ptr<TypeTree> type_tree,
                CreateTreeFromDwarf(type_name, /*from_container=*/true,
                                    resolution_strategy.container_name));

            // Special case for local type resolution. We split the metadata
            // allocation from the backing array allocation, and we can just
            // return the type of the internal raw_hash_set.
            if (IsLocalTypeResolver()) {
              return type_tree;
            }

            ASSIGN_OR_RETURN(
                ObjectLayout template_object_layout,
                TypeTreeContainerBlueprints::GetSwissMapTemplate(
                    type_tree->Name(), type_tree->Root()->GetFullSizeBits(),
                    Alignment, size_t_size, kWidth, request_size * 8,
                    hashtablez_info, hashtablez_info_handle_size));
            std::unique_ptr<TypeTree> outer_tree =
                TypeTree::CreateTreeFromObjectLayout(
                    template_object_layout,
                    WrapType("absl::container_internal::raw_hash_set",
                             type_tree->Name()),
                    "absl::container_internal::raw_hash_set");
            RETURN_IF_ERROR(outer_tree->MergeTreeIntoThis(type_tree.get()));
            if ((!IsLocalTypeResolver() &&
                 request_size != outer_tree->Root()->GetFullSizeBytes()) ||
                (IsLocalTypeResolver() &&
                 request_size % outer_tree->Root()->GetFullSizeBytes() != 0)) {
              return absl::InternalError(BuildErrorMessageInResolution(
                  formal_params, callstack, resolution_strategy,
                  absl::StrCat(
                      "Raw hash set backing array does not match allocation "
                      "size: ",
                      "request_size: ", request_size,
                      " tree size: ", outer_tree->Root()->GetFullSizeBytes())));
            }
            return outer_tree;
          }
        }
      }
      return absl::NotFoundError(BuildErrorMessageInResolution(
          formal_params, callstack, resolution_strategy,
          absl::StrCat("Type name: ", type_data->name)));
    }
    case ContainerResolutionStrategy::kAbseilContainerBtree: {
      // The BtreeNode type resolution relies on a typetree template to
      // resolve all the metadata allocated alongside the client type. The
      // full type resolution entails the following steps:
      // 1. Get the `Alignment` constant of the type from the allocator call.
      // 2. Get the `kNodeSlots` constant from the `Group` class.
      // 3. Get the size of the `field_type` type.
      // 4. Get the size of a pointer type.
      // 5. Get the client type and build the type tree.
      // 6. Build the type tree template for the `BackingArray` struct from
      // all the evaluated constant, request size, and the client type
      // information.
      // 7. Merge the client type tree into the template type tree.
      ASSIGN_OR_RETURN(int64_t Alignment, GetAlignmentFromAbslAllocatorCall(
                                              callstack.at(0).function_name));
      ASSIGN_OR_RETURN(
          const DwarfMetadataFetcher::TypeData* type_data,
          metadata_fetcher_->GetType(resolution_strategy.lookup_type));
      for (const absl::string_view formal_param :
           type_data->formal_parameters) {
        if (absl::StartsWith(formal_param,
                             "absl::container_internal::set_params<") ||
            absl::StartsWith(formal_param,
                             "absl::container_internal::map_params<")) {
          ASSIGN_OR_RETURN(type_data, metadata_fetcher_->GetType(formal_param));

          std::string constant_lookup_type =
              WrapType("absl::container_internal::btree_node", formal_param);

          absl::StatusOr<const DwarfMetadataFetcher::TypeData*>
              generation_typedata = metadata_fetcher_->GetType(
                  "absl::container_internal::btree_iterator_generation_"
                  "info_enabled");

          bool generation_enabled = generation_typedata.ok();

          ASSIGN_OR_RETURN(
              const DwarfMetadataFetcher::TypeData* constant_typedata,
              metadata_fetcher_->GetType(constant_lookup_type));

          auto it = constant_typedata->constant_variables.find("kNodeSlots");
          if (it == constant_typedata->constant_variables.end()) {
            return absl::NotFoundError(BuildErrorMessageInResolution(
                formal_params, callstack, resolution_strategy,
                "No constant variable kNodeSlots found."));
          }
          int64_t kNodeSlots = it->second;

          std::string btree_field_type_name = absl::StrCat(
              WrapType("absl::container_internal::btree", formal_param),
              "::field_type");

          ASSIGN_OR_RETURN(
              const DwarfMetadataFetcher::TypeData* btree_field_type,
              metadata_fetcher_->GetType(btree_field_type_name));
          int64_t btree_field_type_size = btree_field_type->size * 8;

          for (const absl::string_view formal_param_set_params :
               type_data->formal_parameters) {
            for (const absl::string_view allocator_type : kAllocatorWrappers) {
              if (absl::StartsWith(formal_param_set_params, allocator_type)) {
                std::string type_name =
                    UnwrapAndCleanTypeName(formal_param_set_params);
                ASSIGN_OR_RETURN(
                    std::unique_ptr<TypeTree> slot_type_tree,
                    CreateTreeFromDwarf(type_name, /*from_container=*/true,
                                        resolution_strategy.container_name));

                // Special case for local type resolution. We split the metadata
                // allocation from the backing array allocation, and we can just
                // return the type of the internal raw_hash_set.
                if (IsLocalTypeResolver()) {
                  return slot_type_tree;
                }
                ASSIGN_OR_RETURN(
                    ObjectLayout template_object_layout,
                    TypeTreeContainerBlueprints::GetBtreeNodeTypeTemplate(
                        slot_type_tree->Name(),
                        slot_type_tree->Root()->GetFullSizeBits(), Alignment,
                        btree_field_type_size, kNodeSlots,
                        metadata_fetcher_->GetPointerSize() * 8,
                        request_size * 8, generation_enabled));
                std::unique_ptr<TypeTree> btree_node_type_tree =

                    TypeTree::CreateTreeFromObjectLayout(
                        template_object_layout,
                        WrapType("absl::container_internal::btree_node",
                                 slot_type_tree->Name()),
                        "absl::container_internal::btree");
                RETURN_IF_ERROR(btree_node_type_tree->MergeTreeIntoThis(
                    slot_type_tree.get()));
                if (btree_node_type_tree->Root()->GetFullSizeBytes() !=
                    request_size) {
                  return absl::InternalError(BuildErrorMessageInResolution(
                      formal_params, callstack, resolution_strategy,
                      absl::StrCat(
                          "Btree node does not match allocation "
                          "size: ",
                          "request_size: ", request_size, " tree size: ",
                          btree_node_type_tree->Root()->GetFullSizeBytes())));
                }
                return btree_node_type_tree;
              }
            }
          }
        }
      }
      return absl::NotFoundError(BuildErrorMessageInResolution(
          formal_params, callstack, resolution_strategy));
    }

    case ContainerResolutionStrategy::kAbseilContainerInserted: {
      // This is some metadata for containers that we cannot resolve into
      // a type. Keep as char.
      return CreateTreeFromDwarf("char", /*from_container=*/true,
                                 resolution_strategy.container_name);
    }
    default: {
      return absl::InvalidArgumentError(
          "Unknown container type resolution strategy.");
    }
  }
}

absl::StatusOr<std::unique_ptr<TypeTree>>
DwarfTypeResolver::ResolveTypeFromCallstack(const CallStack& callstack,
                                            int64_t request_size) {
  if (callstack.empty()) {
    return absl::InvalidArgumentError("Callstack is empty.");
  }

  // First try to resolve the type from the first frame. This works with
  // non-container heap allocations and requires dwarf extension with
  // DW_TAG_GOOGLE_heapalloc. See cl/647366639 and go/heapalloc-dwarf.
  for (const auto& frame : callstack) {
    absl::StatusOr<std::unique_ptr<TypeTree>> type_tree =
        ResolveTypeFromFrame(frame);
    if (type_tree.ok()) {
      return type_tree;
    }
  }

  // If we couldn't resolve the type from the first frame, try to walk
  // the callstack from the top and try to find container allocation
  // type.
  ASSIGN_OR_RETURN(const ContainerResolutionStrategy resolution_strategy,
                   GetCallStackContainerResolutionStrategy(callstack));
  auto type_tree_or = ResolveTypeFromResolutionStrategy(
      resolution_strategy, callstack, request_size);
  if (type_tree_or.ok()) {
    return type_tree_or;
  } else {
    return type_tree_or.status();
  }
}

absl::StatusOr<std::unique_ptr<TypeTree>>
DwarfTypeResolver::ResolveTypeFromFrame(
    const DwarfMetadataFetcher::Frame& frame) {
  DwarfMetadataFetcher::Frame frame_copy = DwarfMetadataFetcher::Frame(frame);
  absl::StatusOr<std::string> type_name =
      metadata_fetcher_->GetHeapAllocType(frame_copy);

  if (!type_name.ok()) {
    // If we fail, lookup the type name with column 0, in case column values are
    // not contained in dwarf data.
    frame_copy.column = 0;
    ASSIGN_OR_RETURN(type_name,
                     metadata_fetcher_->GetHeapAllocType(frame_copy));
  }
  return CreateTreeFromDwarf(type_name.value(), false, "none");
}

}  // namespace devtools_crosstool_fdo_field_access
