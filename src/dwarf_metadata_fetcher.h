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

#ifndef DWARF_METADATA_FETCHER_H_
#define DWARF_METADATA_FETCHER_H_

#include <stdbool.h>

#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <ostream>
#include <string>
#include <utility>
#include <vector>

#include "absl/base/const_init.h"
#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "absl/synchronization/mutex.h"
#include "binary_file_retriever.h"
#include "llvm/include/llvm/DebugInfo/DWARF/DWARFContext.h"
#include "llvm/include/llvm/DebugInfo/DWARF/DWARFDie.h"
#include "llvm/include/llvm/DebugInfo/DWARF/DWARFFormValue.h"
#include "llvm/include/llvm/DebugInfo/DWARF/DWARFUnit.h"

// A helper type for fetching a type's metadata. Specifically, it can
// 1) fetch a type's metadata from a given type name;
// 2) fetch field metadata from a given offset.
// It reads from either cache directory or Symbol Server to parse the
// DWARF file, and construct local index for later queries.
class DwarfMetadataFetcher {
 private:
  class DwarfParserState {
   public:
    explicit DwarfParserState(std::unique_ptr<llvm::DWARFContext> dwarf_info,
                              const std::string &dwp_file_path)
        : dwp_dwarf_info_(dwp_file_path.empty() ? nullptr
                                                : dwarf_info->getDWOContext(
                                                      dwp_file_path.c_str())),
          dwarf_info_(std::move(dwarf_info)),
          curr_state_(State::kTypesSectionUnits),
          curr_it_(dwarf_info_->types_section_units().begin()),
          m_(absl::kConstInit) {}

    std::unique_ptr<llvm::DWARFUnit> getNextDwarfUnit();

   private:
    enum class State {
      kTypesSectionUnits,
      kInfoSectionUnits,
      kDwpTypesSectionUnits,
      kDwpInfoSectionUnits
    };
    std::shared_ptr<llvm::DWARFContext> dwp_dwarf_info_;
    std::unique_ptr<llvm::DWARFContext> dwarf_info_;
    DwarfParserState::State curr_state_;
    llvm::DWARFUnitVector::iterator curr_it_;
    absl::Mutex m_;
  };

 public:
  struct ParseContext {
    absl::flat_hash_map<uint64_t, std::string> signature_to_type_name;
    ParseContext() = default;
  };

  enum class DataType {
    UNKNOWN,
    CLASS,
    STRUCTURE,
    BASE_TYPE,
    POINTER_LIKE,
    NAMESPACE,
    SUBPROGRAM,
    UNION,
    ENUM
  };
  static std::string DataTypeToStr(DataType data_type) {
    switch (data_type) {
      case DataType::CLASS:
        return "DataType::CLASS";
      case DataType::BASE_TYPE:
        return "DataType::BASE_TYPE";
      case DataType::POINTER_LIKE:
        return "DataType::POINTER_LIKE";
      case DataType::NAMESPACE:
        return "DataType::NAMESPACE";
      case DataType::STRUCTURE:
        return "Datatype::STRUCTURE";
      case DataType::SUBPROGRAM:
        return "Datatype::SUBPROGRAM";
      case DataType::UNION:
        return "Datatype::UNION";
      case DataType::ENUM:
        return "Datatype::ENUM";
      default:
        return "DataType::UNKNOWN";
    }
  }

  // Converts data_type to a short string for visual clarity of types. Only used
  // in dump.
  static std::string DataTypeToShortString(DataType data_type) {
    switch (data_type) {
      case DataType::CLASS:
        return "class";
      case DataType::NAMESPACE:
        return "namespace";
      case DataType::STRUCTURE:
        return "struct";
      case DataType::SUBPROGRAM:
        return "func";
      case DataType::BASE_TYPE:
      case DataType::POINTER_LIKE:
        return "";
      case DataType::UNION:
        return "union";
      case DataType::ENUM:
        return "enum";
      default:
        return "UNKNOWN";
    }
  }

  // Special case for allocator types that wrap the type information into
  // membuf that have type char[N], discarding the actual allocation type. For
  // example map<std::pair<A, B> > will be wrapped with
  // __algined_membuf<std::pair<A, B> >. The membuf itself will have a type
  // char[N], depending on the size of A and B. We unwrap this type instead to
  // continue type resolution. In future, we need to some logic to handle the
  // diff in the size between __aligned_membuf and the internal type, as this
  // could result in padding being added.
  static std::optional<std::string> UnwrapParameterizedStorage(
      absl::string_view type_name);

  struct FieldData {
   public:
    FieldData() = default;
    FieldData(std::string _name, int64_t _offset, std::string _type_name)
        : name(std::move(_name)),
          offset(_offset),
          type_name(std::move(_type_name)) {}
    std::string name;
    int64_t offset;
    std::string type_name;
    bool inherited;

    // Parse field content from a given DIE
    void ParseDIE(const llvm::DWARFDie &die, const ParseContext &context);
  };

  struct Frame {
    std::string function_name;
    uint64_t line_offset;
    uint64_t column;

    explicit Frame(std::string function_name, uint64_t line_offset,
                   uint64_t column)
        : function_name(
              std::move(function_name)),  // Use std::move for efficiency
          line_offset(line_offset),
          column(column) {}

    Frame(const Frame &other) =
        default;  // Default copy constructor is fine here

    bool operator==(const Frame &other) const {
      return function_name == other.function_name &&
             line_offset == other.line_offset && column == other.column;
    }

    bool operator!=(const Frame &other) const { return !(*this == other); }

    template <typename H>
    friend H AbslHashValue(H h, const Frame &f) {
      return H::combine(std::move(h), f.function_name, f.line_offset, f.column);
    }
  };

  // Contains metadata for fields and inside types of a type/namespace.
  struct TypeData {
    TypeData() : size(-1), data_type(DataType::UNKNOWN) {}
    virtual ~TypeData() = default;
    TypeData(std::string _name, int64_t _size)
        : name(std::move(_name)), size(_size), data_type(DataType::UNKNOWN) {}
    // Short name of the type/namespace/type
    std::string name;
    // Real bit size of type in memory.
    int64_t size;
    // data_type represents the actual type of this TypeData
    DataType data_type;
    // Place to store all the fields.
    std::vector<std::unique_ptr<FieldData>> fields;
    // Offset to field's index. Multiple fields can have the same offset due to
    // some type shenanigans. See std::pair<A, B> for example, or
    // std_map_type_test.
    std::map<int64_t, absl::flat_hash_set<size_t>> offset_idx;
    // Mapping from typedef name to the canonical type name
    absl::flat_hash_map<std::string, std::string> typedef_type;
    // Place to store all inside types for this type/namespace
    absl::flat_hash_map<std::string, std::unique_ptr<TypeData>> types;
    // Place to store Formal Parameters, only for Subprograms
    std::vector<std::string> formal_parameters;
    // Map between frame and the type name of the heap allocation made at that
    // source location.
    absl::flat_hash_map<Frame, std::string> heapalloc_sites;
    // Map containing constant variables of a class.
    absl::flat_hash_map<std::string, uint64_t> constant_variables;

    // Parse type/namespace content from a given DIE
    void ParseDIE(const llvm::DWARFDie &die, bool should_read_subprograms,
                  const ParseContext &context);

    bool IsRecordType() const {
      return data_type == DataType::STRUCTURE || data_type == DataType::CLASS;
    }

    virtual void AddType(std::string type_name,
                         std::unique_ptr<TypeData> type) {
      type->name = type_name;
      types[type_name] = std::move(type);
    }

    // Visit child die, recursive parse if needed.
    void VisitChildDIE(const llvm::DWARFDie &child_die,
                       bool should_read_subprogram,
                       const ParseContext &context);

    // Dump information in C++ code format. 'level' is used for indentation,
    // should use 0 at the top level.
    void Dump(std::ostream &out, int level) const;

    void Debug(std::ostream &out, int level) const;
  };

  // A hacky way to get a quick thread safe root type data. We should only ever
  // have a race condition on the types map in the root space.
  struct RootTypeData : TypeData {
    void AddType(std::string type_name,
                 std::unique_ptr<TypeData> type) override {
      m.Lock();
      TypeData::AddType(type_name, std::move(type));
      m.Unlock();
    };

   private:
    absl::Mutex m = absl::Mutex(absl::kConstInit);
  };

  struct BinaryInfo {
    std::string build_id;
    std::string path;

    bool operator==(const BinaryInfo &other) const {
      return build_id == other.build_id && path == other.path;
    }

    bool operator!=(const BinaryInfo &other) const { return !(*this == other); }

    bool operator<(const BinaryInfo &other) const {
      return std::tie(build_id, path) < std::tie(other.build_id, other.path);
    }

    template <typename H>
    friend H AbslHashValue(H h, const BinaryInfo &bi) {
      return H::combine(std::move(h), bi.build_id, bi.path);
    }
  };

  // Helper function that consumes a type held within angle brackets.
  static std::string ConsumeAngleBracket(
      absl::string_view::const_iterator start,
      absl::string_view::const_iterator end);

  DwarfMetadataFetcher(std::unique_ptr<BinaryFileRetriever> file_retriever,
                       std::string cache_dir, bool read_subprograms = false,
                       bool write_to_cache = true,
                       uint32_t parse_thread_count = 1);
  virtual ~DwarfMetadataFetcher() = default;

  // Deserialize from cache directory or send out RPCs to fetch the debugging
  // info, and then construct local index so that later queries can be
  // served immediately.
  virtual absl::Status Fetch(const absl::flat_hash_set<std::string> &build_ids,
                             bool force_update_cache);

  // Same as above, but uses both path and build id to fetch Dwarf data. This
  // allows the DwarfMetadataFetcher to be used with binaries that are not
  // stored in the symbol server.
  virtual absl::Status FetchWithPath(
      const absl::flat_hash_set<BinaryInfo> &build_ids_and_paths,
      bool force_update_cache);

  // Same as above, but only fetches the DWP file. Should be used mostly for
  // testing.
  virtual absl::Status FetchDWPWithPath(
      const absl::flat_hash_set<DwarfMetadataFetcher::BinaryInfo>
          &build_ids_and_paths,
      bool force_update_cache);

  // Returns the type's metadata of the given type name.
  // Should be called before 'Fetch', result is valid until next 'Fetch'.
  // The type_name should be the type_name name with namespace(s).
  virtual absl::StatusOr<const TypeData *> GetType(absl::string_view) const;

  // Same as above, but caches the type data. This is useful for cases where
  // the type data is needed multiple times.
  virtual absl::StatusOr<const TypeData *> GetCacheableType(
      absl::string_view type_name);

  // Returns a type field member's metadata of the given type name and
  // offset (a field will be returned if it covers the offset).
  // Should be called before 'Fetch', result is valid until next 'Fetch'.
  virtual absl::StatusOr<const FieldData *> GetField(absl::string_view,
                                                     int64_t offset) const;

  // Returns the type name of a heap allocation made at a source location, or in
  // other words, a frame. Intended for use cases where the CallStack of
  // an allocation is known, but the type is unknown.
  virtual absl::StatusOr<std::string> GetHeapAllocType(
      const Frame &frame) const;

  virtual absl::StatusOr<std::vector<std::string>> GetFormalParameters(
      absl::string_view linkage_name) const;

  // Return pointer size.
  int64_t GetPointerSize() { return pack_.pointer_size; }

  // Dump type metadata info in C++ format to the given ostream.
  void Dump(std::ostream &out) const;

  // Split the namespace(s) (or type/function name(s)) from a full name.
  // e.g., "AAA::BBB<T>::CCC(aaa)" => returns {"AAA", "BBB<T>", "CCC(aaa)"}
  static std::vector<absl::string_view> SplitNamespace(
      absl::string_view type_name);

 private:
  struct MetadataPack {
    MetadataPack()
        : pointer_size(0), root_space(std::make_unique<RootTypeData>()) {}

    // Read relevant debugging info from given file to construct local index.
    absl::Status ParseDWARF(absl::string_view bin_file_path,
                            const std::string &dwp_file_path,
                            bool should_read_subprogram,
                            uint32_t parse_thread_count);

    // Update 'pointer_size_' if not updated before. Return error if the
    // new_size is not consistent with the previous size.
    absl::Status TryUpdatePointerSize(int64_t new_size);

    // Read and insert the content from another MetadataPack.
    absl::Status Insert(MetadataPack &other);

    // Check if this pack is empty or not.
    bool Empty() const;

    // Byte size of the pointer/address of the given debugging info file.
    int64_t pointer_size;

    // Root to store all metadata.
    std::unique_ptr<RootTypeData> root_space;

    // Map between between identifiers and their respective Formal
    // Parameters.
    absl::flat_hash_map<std::string, std::vector<std::string>>
        formal_and_template_param_map;

    // Map between frame and the type name of the heap allocation made at that
    // source location.
    absl::flat_hash_map<Frame, std::string> heapalloc_sites;

    // Go through all Subprograms to index them for fast lookup, populating
    // subprogram_data map. Also adds sizes to TypeData with DataType
    // pointer_like.
    absl::Status PostProcessAndIndexTypeData(TypeData *type_data,
                                             std::string namespace_ctxt);
  };

  // This function walks recursively through the TypeData tree to find the
  // TypeData that matches the list of names split by namespace from the full
  // unqualified type name. If there is no match, it continues looking in the
  // anonymous namespace. If there is a typedef, we restart the search since the
  // namespace context is reset.
  absl::StatusOr<const DwarfMetadataFetcher::TypeData *> SearchType(
      const DwarfMetadataFetcher::TypeData *parent_type,
      const std::vector<absl::string_view> &names, int cur) const;

  // Read the DWARF content of the binary/dwp file to the given MetadataPack.
  // TODO: b/344968545 - ReadFromDwarf should be refactored to use
  // absl::string_view. Requires refactoring BinaryFileRetriever first.
  absl::Status ReadFromDWARF(const std::string &build_id,
                             const std::string &path, MetadataPack *pack_ptr);

  // Use for downloading debugging info file(s) from symbol server.
  std::unique_ptr<BinaryFileRetriever> file_retriever_;

  // Contains the indexed type/field metadata data parsed from DWARF.
  MetadataPack pack_;

  // Directory contains cache data serialization files.
  std::string cache_dir_;

  // Option to set whether or not DwarfMetadatafetcher should populate
  // Subprogram data.
  bool should_read_subprograms_;

  // Option to set whether or not DwarfMetadatafetcher should write to cache.
  bool write_to_cache_;

  // Option to set the number of threads to use for parsing DWARF files.
  uint32_t parse_thread_count_;

  // Cache of type data.
  absl::flat_hash_map<std::string, const TypeData *> cache_;
};

#endif  // DWARF_METADATA_FETCHER_H_
