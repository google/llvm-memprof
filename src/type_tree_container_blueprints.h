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

#ifndef TYPE_TREE_CONTAINER_BLUEPRINTS_H_
#define TYPE_TREE_CONTAINER_BLUEPRINTS_H_

// This header file holds builds template type trees for containers that have
// special allocations and metadata associated with each allocation. This
// includes all absl::btree and all absl::raw_hash_set containers.

#include <google/protobuf/text_format.h>

#include <cstdint>
#include <string>

#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/strings/substitute.h"
#include "src/object_layout.pb.h"

namespace devtools_crosstool_fdo_field_access {

static int64_t RoundUpTo(int64_t number, int64_t multiple) {
  return (number + multiple - 1) / multiple * multiple;
}

constexpr absl::string_view kAbslTemplatePadding = R"pb(
  subobjects: {
    properties: {
      name: "",
      type_name: "",
      kind: PADDING,
      type_kind: PADDING_TYPE,
      size_bits: $0,
      multiplicity: 1,
    },
  },
)pb";

constexpr absl::string_view kAbslBtreeNodeStart = R"pb(
  properties: {
    name: "absl::container_internal::btree_node<$1>",
    type_name: "absl::container_internal::btree_node<$1>",
    kind: BASE,
    type_kind: RECORD_TYPE,
    size_bits: 0,
    multiplicity: 1,
  },
  subobjects: {
    properties: {
      name: "parent",
      type_name: "btree_node *",
      kind: FIELD,
      type_kind: BUILTIN_TYPE,
      size_bits: $0,
      multiplicity: 1,
    },
  },
)pb";

constexpr absl::string_view kAbslBtreeNodeGenerationField = R"pb(
  subobjects {
    properties: {
      name: "generation",
      type_name: "uint32_t",
      kind: FIELD,
      type_kind: BUILTIN_TYPE,
      size_bits: 32,
      multiplicity: 1,
    },
  },
)pb";

constexpr absl::string_view kAbslBtreeNodeMetadataFields = R"pb(
  subobjects {
    properties: {
      name: "position",
      type_name: "node_count_type",
      kind: FIELD,
      type_kind: BUILTIN_TYPE,
      size_bits: $0,
      multiplicity: 1,
    },
  },
  subobjects {
    properties: {
      name: "start",
      type_name: "node_count_type",
      kind: FIELD,
      type_kind: BUILTIN_TYPE,
      size_bits: $0,
      multiplicity: 1,
    },
  },
  subobjects {
    properties: {
      name: "finish",
      type_name: "node_count_type",
      kind: FIELD,
      type_kind: BUILTIN_TYPE,
      size_bits: $0,
      multiplicity: 1,
    },
  },
  subobjects {
    properties: {
      name: "max_count",
      type_name: "node_count_type",
      kind: FIELD,
      type_kind: BUILTIN_TYPE,
      size_bits: $0,
      multiplicity: 1,
    },
  },
)pb";

constexpr absl::string_view kAbslBtreeNodeSlots = R"pb(
  subobjects {
    properties: {
      name: "values",
      type_name: "$0[$1]",
      kind: FIELD,
      type_kind: ARRAY_TYPE,
      size_bits: 0,
      multiplicity: 1,
    },

    subobjects:
    [ {
      properties: {
        name: "[_]",
        type_name: "$0",
        kind: ARRAY_ELEMENTS,
        type_kind: RECORD_TYPE,
        size_bits: $2,
        multiplicity: $1,
      },
    }],
  },
)pb";

constexpr absl::string_view kAbslBtreeNodeChildren = R"pb(
  subobjects {
    properties: {
      name: "children",
      type_name: "btree_node *[$1]",
      kind: FIELD,
      type_kind: ARRAY_TYPE,
      size_bits: 0,
      multiplicity: 1,
    },
    subobjects:
    [ {
      properties: {
        name: "[_]",
        type_name: "btree_node *",
        kind: ARRAY_ELEMENTS,
        type_kind: BUILTIN_TYPE,
        size_bits: $0,
        multiplicity: $1,
      },
    }],
  },
)pb";

constexpr absl::string_view kAbslSwissMapTemplateStart = R"pb(
  properties: {
    name: "absl::container_internal::raw_hash_set::BackingArray<$0>",
    type_name: "absl::container_internal::raw_hash_set::BackingArray<$0>",
    kind: BASE,
    type_kind: RECORD_TYPE,
    size_bits: 0,
    multiplicity: 1,
  },
)pb";

constexpr absl::string_view kAbslSwissMapTemplateHashTableZ = R"pb(
  subobjects: {
    properties: {
      name: "infoz_",
      type_name: "HashtablezInfoHandle",
      kind: FIELD,
      type_kind: BUILTIN_TYPE,
      size_bits: $0,
      multiplicity: 1,
    },
  },
)pb";
constexpr absl::string_view kAbslSwissMapTemplateGrowthLeft = R"pb(
  subobjects: {
    properties: {
      name: "growth_left",
      type_name: "size_t",
      kind: FIELD,
      type_kind: BUILTIN_TYPE,
      size_bits: $0,
      multiplicity: 1,
    },
  },
)pb";

constexpr absl::string_view kAbslSwissMapTemplateFields = R"pb(
  subobjects: {
    properties: {
      name: "ctrl",
      type_name: "ctrl_t[$0]",
      kind: FIELD,
      type_kind: ARRAY_TYPE,
      size_bits: 0,
      multiplicity: 1,
    },
    subobjects:
    [ {
      properties: {
        name: "[_]",
        type_name: "ctrl_t",
        kind: ARRAY_ELEMENTS,
        type_kind: BUILTIN_TYPE,
        size_bits: 8,
        multiplicity: $0,
      },
    }],
  },
  subobjects: {
    properties: {
      name: "sentinel",
      type_name: "ctrl_t",
      kind: FIELD,
      type_kind: ARRAY_TYPE,
      size_bits: 8,
      multiplicity: 1,
    },
  },
  subobjects: {
    properties: {
      name: "clones",
      type_name: "ctrl_t[$1]",
      kind: FIELD,
      type_kind: ARRAY_TYPE,
      size_bits: 0,
      multiplicity: 1,
    },
    subobjects:
    [ {
      properties: {
        name: "[_]",
        type_name: "ctrl_t",
        kind: ARRAY_ELEMENTS,
        type_kind: BUILTIN_TYPE,
        size_bits: 8,
        multiplicity: $1,
      },
    }],
  },
)pb";

constexpr absl::string_view kAbslSwissMapTemplateSlots = R"pb(
  subobjects {
    properties: {
      name: "slots",
      type_name: "$1[$0]",
      kind: FIELD,
      type_kind: ARRAY_TYPE,
      size_bits: 0,
      multiplicity: 1,
    },

    subobjects:
    [ {
      properties: {
        name: "[_]",
        type_name: "$1",
        kind: ARRAY_ELEMENTS,
        type_kind: RECORD_TYPE,
        size_bits: $2,
        multiplicity: $0,
      },
    }],
  },
)pb";

class TypeTreeContainerBlueprints {
 public:
  static absl::StatusOr<ObjectLayout> GetBtreeNodeTypeTemplate(
      absl::string_view slot_type_name, int64_t slot_type_size,
      int64_t Alignment, int64_t field_type_size, int64_t kNodeSlots,
      int64_t pointer_size, int64_t request_size,
      bool absl_btree_enable_generations) {
    // Sizes of all fields: parent, generation, position, start, finish,
    // max_count, values, children.
    int64_t node_static_size = pointer_size + field_type_size * 4 +
                               (absl_btree_enable_generations ? 32 : 0);

    int64_t node_static_size_aligned = RoundUpTo(node_static_size, Alignment);
    int64_t padding_size = node_static_size_aligned - node_static_size;
    int64_t variable_size = request_size - node_static_size_aligned;
    int64_t children_size = (kNodeSlots + 1) * pointer_size;

    int64_t number_of_slots;
    bool is_leaf;

    if ((variable_size - children_size) % slot_type_size == 0) {
      // In this case we have an internal node.
      number_of_slots = (variable_size - children_size) / slot_type_size;
      is_leaf = false;
    } else if (variable_size % slot_type_size == 0) {
      // In this case we have a leaf node.
      number_of_slots = variable_size / slot_type_size;
      is_leaf = true;
    } else {
      return absl::InvalidArgumentError(
          "Size mismatch in creating BtreeNodeTemplate, slots do not fit into "
          "type.");
    }

    // We use absl::Substitute instead of proto fixture (go/proto-fixtures),
    // because fixtures do not support repeated fields.
    std::string resolved_template_string = absl::StrCat(
        absl::Substitute(kAbslBtreeNodeStart, pointer_size, slot_type_name),
        absl_btree_enable_generations ? kAbslBtreeNodeGenerationField : "",
        absl::Substitute(kAbslBtreeNodeMetadataFields, field_type_size),
        padding_size > 0 ? absl::Substitute(kAbslTemplatePadding, padding_size)
                         : "",
        absl::Substitute(kAbslBtreeNodeSlots, slot_type_name, number_of_slots,
                         slot_type_size),
        !is_leaf ? absl::Substitute(kAbslBtreeNodeChildren, pointer_size,
                                    kNodeSlots + 1)
                 : "");

    ObjectLayout template_object_layout;
    if (!google::protobuf::TextFormat::ParseFromString(
            resolved_template_string, &template_object_layout)) {
      return absl::InternalError("Failed to parse resolved template string.");
    }
    return template_object_layout;
  }

  static absl::StatusOr<ObjectLayout> GetSwissMapTemplate(
      absl::string_view slot_type_name, int64_t slot_type_size,
      int64_t Alignment, int64_t size_t_size, int64_t kWidth,
      int64_t request_size, bool has_hash_table_z,
      int64_t hashtablez_handle_size) {
    has_hash_table_z = false;
    int64_t capacity =
        (request_size - (kWidth - 1) * 8 - size_t_size) / (slot_type_size + 8);
    int64_t metadata_size = (has_hash_table_z ? hashtablez_handle_size : 0) +
                            size_t_size + (capacity + kWidth) * 8;

    int64_t metadata_plus_padding = RoundUpTo(metadata_size, Alignment * 8);
    int64_t padding_size = metadata_plus_padding - metadata_size;

    std::string resolved_template_string = absl::StrCat(
        absl::Substitute(kAbslSwissMapTemplateStart, slot_type_name),
        has_hash_table_z ? absl::Substitute(kAbslSwissMapTemplateHashTableZ,
                                            hashtablez_handle_size)
                         : "",
        absl::Substitute(kAbslSwissMapTemplateGrowthLeft, size_t_size),
        absl::Substitute(kAbslSwissMapTemplateFields, capacity, kWidth - 1),
        padding_size > 0 ? absl::Substitute(kAbslTemplatePadding, padding_size)
                         : "",
        absl::Substitute(kAbslSwissMapTemplateSlots, capacity, slot_type_name,
                         slot_type_size));

    ObjectLayout template_object_layout;
    if (!google::protobuf::TextFormat::ParseFromString(
            resolved_template_string, &template_object_layout)) {
      return absl::InternalError("Failed to parse resolved template string.");
    }
    return template_object_layout;
  }
};

}  // namespace devtools_crosstool_fdo_field_access

#endif  // TYPE_TREE_CONTAINER_BLUEPRINTS_H_
