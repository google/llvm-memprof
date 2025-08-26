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

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <memory>
#include <optional>
#include <ostream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/container/flat_hash_set.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/ascii.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "absl/strings/string_view.h"
#include "absl/synchronization/mutex.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "binary_file_retriever.h"
#include "status_macros.h"
#include "llvm/include/llvm/BinaryFormat/Dwarf.h"
#include "llvm/include/llvm/DebugInfo/DIContext.h"
#include "llvm/include/llvm/DebugInfo/DWARF/DWARFDie.h"
#include "llvm/include/llvm/DebugInfo/DWARF/DWARFFormValue.h"
#include "llvm/include/llvm/DebugInfo/DWARF/DWARFTypeUnit.h"
#include "llvm/include/llvm/DebugInfo/DWARF/DWARFUnit.h"
#include "llvm/include/llvm/Object/Binary.h"
#include "llvm/include/llvm/Object/ObjectFile.h"
#include "llvm/include/llvm/Support/Debug.h"
#include "llvm/include/llvm/Support/raw_ostream.h"

// Wrappers for allocated types.
const absl::string_view kMembufWrappers[] = {
    "__gnu_cxx::__aligned_membuf", // in std::map and std::set
    "__gnu_cxx::__aligned_buffer", // in unordered_map and unordered_set
};

constexpr std::string_view kAnonPrefix = "Anon_";
constexpr std::string_view kAnonSigPrefix = "AnonSig_";

DwarfMetadataFetcher::DwarfMetadataFetcher(
    std::unique_ptr<BinaryFileRetriever> file_retriever, std::string cache_dir,
    bool should_read_subprograms, bool write_to_cache,
    uint32_t parse_thread_count)
    : file_retriever_(std::move(file_retriever)), cache_dir_(cache_dir),
      should_read_subprograms_(should_read_subprograms),
      write_to_cache_(write_to_cache), parse_thread_count_(parse_thread_count) {
}

absl::Status DwarfMetadataFetcher::ReadFromDWARF(const std::string &build_id,
                                                 const std::string &path,
                                                 MetadataPack *pack_ptr) {
  auto binary_or = file_retriever_->RetrieveBinary(build_id, path);
  auto dwp_or = file_retriever_->RetrieveDwpFile(build_id);
  if (binary_or.status().ok() && dwp_or.status().ok()) {
    RETURN_IF_ERROR(pack_ptr->ParseDWARF(binary_or.value(), dwp_or.value(),
                                         should_read_subprograms_,
                                         parse_thread_count_));
  } else if (binary_or.status().ok()) {
    LOG(WARNING) << "Failed to get dwp for build_id" << build_id;
    RETURN_IF_ERROR(pack_ptr->ParseDWARF(
        binary_or.value(), "", should_read_subprograms_, parse_thread_count_));
  } else {
    LOG(WARNING) << "Failed to get binary and dwp for build_id" << build_id;
  }
  return absl::OkStatus();
}

// Used only for nicer error message printing.
std::string MergeNames(const std::vector<absl::string_view> &names) {
  return absl::StrJoin(names, "::");
}

absl::Status DwarfMetadataFetcher::FetchWithPath(
    const absl::flat_hash_set<DwarfMetadataFetcher::BinaryInfo>
        &build_ids_and_paths,
    bool force_update_cache) {
  pack_ = MetadataPack();
  for (auto &bin_info : build_ids_and_paths) {
    LOG(INFO) << "Process build_id: " << bin_info.build_id;
    MetadataPack pack;
    if (force_update_cache) {
      LOG(INFO) << "Read from DWARF content instead of cache";
      RETURN_IF_ERROR(ReadFromDWARF(bin_info.build_id, bin_info.path, &pack));
    }
    RETURN_IF_ERROR(
        pack.PostProcessAndIndexTypeData(pack.root_space.get(), ""));
    RETURN_IF_ERROR(pack_.Insert(pack));
  }
  return absl::OkStatus();
}

absl::Status DwarfMetadataFetcher::FetchDWPWithPath(
    const absl::flat_hash_set<DwarfMetadataFetcher::BinaryInfo>
        &build_ids_and_paths,
    bool force_update_cache) {
  pack_ = MetadataPack();
  for (auto &bin_info : build_ids_and_paths) {
    std::string dwp_path = bin_info.path;
    RETURN_IF_ERROR(pack_.ParseDWARF(
        dwp_path, dwp_path, should_read_subprograms_, parse_thread_count_));
    RETURN_IF_ERROR(
        pack_.PostProcessAndIndexTypeData(pack_.root_space.get(), ""));
  }
  return absl::OkStatus();
}

absl::Status
DwarfMetadataFetcher::Fetch(const absl::flat_hash_set<std::string> &build_ids,
                            bool force_update_cache) {
  absl::flat_hash_set<DwarfMetadataFetcher::BinaryInfo> build_ids_and_paths;
  for (const auto &build_id : build_ids) {
    build_ids_and_paths.insert({.build_id = build_id, .path = ""});
  }
  return FetchWithPath(build_ids_and_paths, force_update_cache);
}

absl::Status DwarfMetadataFetcher::MetadataPack::PostProcessAndIndexTypeData(
    TypeData *type_data, std::string namespace_ctxt) {
  if (type_data == nullptr) {
    return absl::OkStatus();
  }

  if (type_data->data_type == DataType::NAMESPACE && !type_data->name.empty()) {
    absl::StrAppend(&namespace_ctxt, "::", type_data->name);
  }
  heapalloc_sites.merge(type_data->heapalloc_sites);

  if (!type_data->formal_parameters.empty()) {
    if (type_data->data_type == DataType::SUBPROGRAM) {
      formal_and_template_param_map.insert(
          {type_data->name, type_data->formal_parameters});
    } else {
      // Add full name with namespace to the formal parameters map.
      formal_and_template_param_map.insert(
          {absl::StrCat(namespace_ctxt, "::", type_data->name),
           type_data->formal_parameters});
    }
  }

  if (type_data->data_type == DataType::POINTER_LIKE) {
    type_data->size = pointer_size;
  }

  for (auto &[unused, child_type_data] : type_data->types) {
    RETURN_IF_ERROR(
        PostProcessAndIndexTypeData(child_type_data.get(), namespace_ctxt));
  }
  return absl::OkStatus();
}

static std::optional<std::string>
ResolveSignature(const llvm::DWARFDie &die,
                 const DwarfMetadataFetcher::ParseContext &context) {
  std::optional<llvm::DWARFFormValue> sig_value =
      die.find(llvm::dwarf::DW_AT_signature);
  if (sig_value) {
    std::optional<uint64_t> sig_value_as_unsigned =
        llvm::dwarf::toSignatureReference(sig_value);
    if (!sig_value_as_unsigned) {
      LOG(ERROR) << "Failed to get signature value for die: "
                 << die.getOffset();
      return std::nullopt;
    }
    auto it = context.signature_to_type_name.find(*sig_value_as_unsigned);
    if (it != context.signature_to_type_name.end()) {
      return it->second;
    } else {
      LOG(ERROR) << "signature not found in context: "
                 << *sig_value_as_unsigned;
      return std::nullopt;
    }
  }
  return std::nullopt;
}

// Recursively get to root type definition die.
static llvm::DWARFDie RecursiveGetTypeDIE(const llvm::DWARFDie &die) {
  if (!die.isValid()) {
    return die;
  }

  switch (die.getTag()) {
  case llvm::dwarf::DW_TAG_structure_type:
  case llvm::dwarf::DW_TAG_array_type:
  case llvm::dwarf::DW_TAG_class_type:
  case llvm::dwarf::DW_TAG_base_type:
  case llvm::dwarf::DW_TAG_pointer_type:
  case llvm::dwarf::DW_TAG_reference_type:
  case llvm::dwarf::DW_TAG_union_type: {
    return die;
  }
  default:
    return RecursiveGetTypeDIE(
        die.getAttributeValueAsReferencedDie(llvm::dwarf::DW_AT_type));
  }
}

static llvm::DWARFDie RecursiveGetTypedefDIE(const llvm::DWARFDie &die) {
  llvm::DWARFDie cur = die;
  while (cur.isValid() && cur.getTag() == llvm::dwarf::DW_TAG_typedef) {
    cur = cur.getAttributeValueAsReferencedDie(llvm::dwarf::DW_AT_type);
  }
  return cur;
}

// Recursively get to root type definition die for formalparam.
static llvm::DWARFDie
RecursiveGetTypeDIEFormalParam(const llvm::DWARFDie &die) {
  if (!die.isValid()) {
    return die;
  }

  switch (die.getTag()) {
  case llvm::dwarf::DW_TAG_structure_type:
  case llvm::dwarf::DW_TAG_class_type:
  case llvm::dwarf::DW_TAG_base_type:
  case llvm::dwarf::DW_TAG_union_type:
    return die;
  default:
    return RecursiveGetTypeDIEFormalParam(
        die.getAttributeValueAsReferencedDie(llvm::dwarf::DW_AT_type));
  }
}

// // This gets the full name, including all namespaces. This step is expensive.
// // If we see that the DwarfMetadatFetcher becomes bottleneck or takes up too
// // much time in tooling, the parsing should be refactored to avoid this step.
// // One option is to use a namespace context in ParseDIE.
// static std::string GetTypeQualifiedName(const llvm::DWARFDie& die) {
//   std::string full_type_name = "";
//   llvm::raw_string_ostream output(full_type_name);
//   llvm::dumpTypeQualifiedName(die, output);
//   output.flush();
//   return full_type_name;
// }

// Get the real name of the given 'die'. It recursively explores the
// name if the 'die' is of pointer-like type.
static std::string RecursiveGetName(const llvm::DWARFDie &die) {
  if (!die.isValid()) {
    die.dump();
    return "";
  }

  if (const char *name = die.getName(llvm::DINameKind::ShortName)) {
    return name;
  }
  std::string sub_name = RecursiveGetName(
      die.getAttributeValueAsReferencedDie(llvm::dwarf::DW_AT_type));
  switch (die.getTag()) {
  case llvm::dwarf::DW_TAG_array_type:
    return absl::StrCat(sub_name, "[]");
  case llvm::dwarf::DW_TAG_pointer_type:
  case llvm::dwarf::DW_TAG_ptr_to_member_type:
    return absl::StrCat(sub_name, "*");
  case llvm::dwarf::DW_TAG_reference_type:
    return absl::StrCat(sub_name, "&");
  case llvm::dwarf::DW_TAG_rvalue_reference_type:
    return absl::StrCat(sub_name, "&&");
  default:
    return sub_name;
  }
}

static std::string RecursiveGetNameOrResolveAnon(const llvm::DWARFDie &die) {
  std::string name = RecursiveGetName(die);
  if (name.empty()) {
    uint64_t offset = die.getOffset();
    name = absl::StrCat(kAnonPrefix, offset);
  }
  return name;
}

// This gets the full name, including all namespaces. This step is expensive.
// If we see that the DwarfMetadatFetcher becomes bottleneck or takes up too
// much time in tooling, the parsing should be refactored to avoid this step.
// One option is to use a namespace context in ParseDIE.
static std::string GetTypeQualifiedName(const llvm::DWARFDie &die) {
  std::string full_type_name = "";
  llvm::raw_string_ostream output(full_type_name);
  llvm::dumpTypeQualifiedName(die, output);
  output.flush();
  return full_type_name;
}

absl::Status
DwarfMetadataFetcher::MetadataPack::TryUpdatePointerSize(int64_t new_size) {
  if (pointer_size == 0) {
    pointer_size = new_size;
  } else if (pointer_size != new_size) {
    return absl::InvalidArgumentError(
        "The address byte size is inconsistent in the debug info file");
  }
  return absl::OkStatus();
}

absl::Status DwarfMetadataFetcher::MetadataPack::ParseDWARF(
    absl::string_view bin_file_path, const std::string &dwp_file_path,
    bool should_read_subprogram, uint32_t parse_thread_count_) {
  LOG(INFO) << "parsing dwarf file: " << bin_file_path;
  auto object_owning_binary_or_err = llvm::object::ObjectFile::createObjectFile(
      llvm::StringRef(bin_file_path));
  if (!object_owning_binary_or_err) {
    llvm::Error err = object_owning_binary_or_err.takeError();
    return absl::InternalError("Cannot create object file: " +
                               llvm::toString(std::move(err)));
  }

  llvm::object::OwningBinary<llvm::object::ObjectFile> object_binary(
      std::move(object_owning_binary_or_err.get()));

  auto dwarf_info = llvm::DWARFContext::create(
      *object_binary.getBinary(),
      llvm::DWARFContext::ProcessDebugRelocations::Ignore, nullptr,
      dwp_file_path);

  auto VisitSibAndChildren = [this, should_read_subprogram](
                                 const std::unique_ptr<llvm::DWARFUnit> &unit,
                                 const ParseContext &context) -> absl::Status {
    RETURN_IF_ERROR(this->TryUpdatePointerSize(unit->getAddressByteSize()));
    llvm::DWARFDie sib_die = unit->getUnitDIE(false);
    while (sib_die) {
      llvm::DWARFDie child_die = sib_die.getFirstChild();
      while (child_die) {
        this->root_space->VisitChildDIE(child_die, should_read_subprogram,
                                        context);
        child_die = child_die.getSibling();
      }
      sib_die = sib_die.getSibling();
    }
    return absl::OkStatus();
  };

  absl::Time start_time = absl::Now();
  ParseContext context;

  if (!dwp_file_path.empty()) {
    auto dwp_dwarf_info = dwarf_info->getDWOContext(dwp_file_path.c_str());
    LOG(INFO) << "Looking for type units ...";

    for (const std::unique_ptr<llvm::DWARFUnit> &unit :
         dwp_dwarf_info->dwo_types_section_units()) {
      if (unit->isTypeUnit()) {
        llvm::DWARFTypeUnit *type_unit =
            dynamic_cast<llvm::DWARFTypeUnit *>(unit.get());
        if (type_unit == nullptr) {
          continue;
        }
        llvm::DWARFDie TD = type_unit->getDIEForOffset(
            type_unit->getTypeOffset() + type_unit->getOffset());
        std::string type_name = GetTypeQualifiedName(TD);
        context.signature_to_type_name[type_unit->getTypeHash()] = type_name;
        if (type_name.empty() || type_name.ends_with("class ") ||
            type_name.ends_with("union ") ||
            type_name.ends_with("structure ")) {
          type_name = absl::StripAsciiWhitespace(type_name);
          absl::StrAppend(&type_name, "_", kAnonSigPrefix,
                          type_unit->getTypeHash());
        }
      }
    }
    for (const std::unique_ptr<llvm::DWARFUnit> &unit :
         dwp_dwarf_info->dwo_info_section_units()) {
      if (unit->isTypeUnit()) {
        llvm::DWARFTypeUnit *type_unit =
            dynamic_cast<llvm::DWARFTypeUnit *>(unit.get());
        if (type_unit == nullptr) {
          continue;
        }
        llvm::DWARFDie TD = type_unit->getDIEForOffset(
            type_unit->getTypeOffset() + type_unit->getOffset());
        std::string type_name = GetTypeQualifiedName(TD);
        if (type_name.empty() || type_name.ends_with("class ") ||
            type_name.ends_with("union ") ||
            type_name.ends_with("structure ")) {
          type_name = absl::StripAsciiWhitespace(type_name);
          absl::StrAppend(&type_name, "_", kAnonSigPrefix,
                          type_unit->getTypeHash());
        }

        context.signature_to_type_name[type_unit->getTypeHash()] = type_name;
      }
    }

    LOG(INFO) << "Start parsing dwp file ...";
    for (const std::unique_ptr<llvm::DWARFUnit> &unit :
         dwp_dwarf_info->dwo_types_section_units()) {
      RETURN_IF_ERROR(VisitSibAndChildren(unit, context));
    }
    for (const std::unique_ptr<llvm::DWARFUnit> &unit :
         dwp_dwarf_info->dwo_info_section_units()) {
      RETURN_IF_ERROR(VisitSibAndChildren(unit, context));
    }
  }

  LOG(INFO) << "Start parsing binary file ...";
  for (const std::unique_ptr<llvm::DWARFUnit> &unit :
       dwarf_info->types_section_units()) {
    RETURN_IF_ERROR(VisitSibAndChildren(unit, context));
  }
  for (const std::unique_ptr<llvm::DWARFUnit> &unit :
       dwarf_info->info_section_units()) {
    // unit->dump(llvm::outs(), llvm::DIDumpOptions());
    RETURN_IF_ERROR(VisitSibAndChildren(unit, context));
  }

  /* ======== Multithreaded version: Some issues for now ======== */

  // for (const auto& [signature, type_name] : context.signature_to_type_name) {
  //   llvm::errs() << "signature: " << signature << " type_name: " << type_name
  //                << "\n";
  //   absl::StatusOr<TypeData*> type_data = SearchType(type_name);
  //   if (!type_data.ok()) {
  //     llvm::errs() << "type_data not ok: " << type_data.status() << "\n";
  //   }
  // }

  // for (const std::unique_ptr<llvm::DWARFUnit>& unit :
  //      dwp_dwarf_info->dwo_compile_units()) {
  //   dwp_dwarf_info->dump(llvm::outs(), llvm::DIDumpOptions());
  //   // RETURN_IF_ERROR(VisitSibAndChildren(unit));
  // }

  // DwarfParserState state(std::move(dwarf_info), dwp_file_path);

  // thread::Bundle parser_threads;

  // for (uint32_t i = 0; i < parse_thread_count_; ++i) {
  //   parser_threads.Add([&state, &VisitSibAndChildren]() -> void {
  //     std::unique_ptr<llvm::DWARFUnit> unit = state.getNextDwarfUnit();
  //     while (unit) {
  //       auto status = VisitSibAndChildren(unit);
  //       if (!status.ok()) {
  //         LOG(ERROR) << "Failed to visit sib and children: " << status;
  //       }
  //       unit = state.getNextDwarfUnit();
  //     }
  //   });
  // }
  // parser_threads.JoinAll();

  /* ======== End of Multithreaded version: Issues with muppet for now ========
   */

  absl::Time end_time = absl::Now();
  LOG(INFO) << "Parsing took " << end_time - start_time;
  return absl::OkStatus();
}

void DwarfMetadataFetcher::FieldData::ParseDIE(const llvm::DWARFDie &die,
                                               const ParseContext &context) {
  name = RecursiveGetName(die);
  offset = -1;
  auto optional_value = die.find(llvm::dwarf::DW_AT_data_member_location);
  if (optional_value.has_value()) {
    offset = optional_value.value().getRawUValue();
  }
  if (die.getTag() == llvm::dwarf::DW_TAG_inheritance) {
    inherited = true;
  }
  llvm::DWARFDie type_die = RecursiveGetTypeDIE(die);
  auto signature_type_name = ResolveSignature(type_die, context);
  if (signature_type_name) {
    type_name = *signature_type_name;
  } else {
    type_name = GetTypeQualifiedName(type_die);
  }

  if (type_name.ends_with("::union ") || type_name.ends_with("::class ") ||
      type_name.ends_with("::structure ")) {
    // We are referring to an anonymous union or class. Replace name with
    // special name based on the offset.
    auto at_type_attribute = die.find(llvm::dwarf::DW_AT_type);
    size_t lastDoubleColonPos = type_name.rfind("::");
    if (lastDoubleColonPos != std::string::npos) {
      type_name = type_name.substr(0, lastDoubleColonPos + 2);
    }
    if (at_type_attribute.has_value()) {
      llvm::DWARFDie referenced_die =
          die.getAttributeValueAsReferencedDie(llvm::dwarf::DW_AT_type);
      if (referenced_die.isValid()) {
        type_name =
            absl::StrCat(type_name, kAnonPrefix, referenced_die.getOffset());
      }
    }
  }
}

std::string DwarfMetadataFetcher::ConsumeAngleBracket(
    const absl::string_view::const_iterator start,
    const absl::string_view::const_iterator end) {
  // Unwrap membuf typename. This means we consume angle brackets and return
  // insides.
  size_t opened = 0;
  size_t closed = 0;
  absl::string_view::const_iterator new_end = end, new_start = start;
  auto curr = start;
  for (; curr != end; ++curr) {
    if (*curr == '>') {
      closed++;
      if (closed == opened) {
        // Remove any trailing whitespace
        new_end = *(curr - 1) == ' ' ? curr - 1 : curr;
        break;
      }
    }
    if (*curr == '<') {
      if (opened == 0) {
        new_start = curr + 1;
      }
      opened++;
    }
  }
  return std::string(new_start, new_end);
}

std::optional<std::string>
DwarfMetadataFetcher::UnwrapParameterizedStorage(absl::string_view type_name) {
  for (const auto &wrapper : kMembufWrappers) {
    if (type_name.compare(0, wrapper.size(), wrapper) == 0) {
      // Unwrap membuf typename. This means we consume angle brackets and
      // return insides.
      return ConsumeAngleBracket(type_name.begin(), type_name.end());
    }
  }
  return std::nullopt;
}

void DwarfMetadataFetcher::Dump(std::ostream &out) const {
  pack_.root_space->Dump(out, /*level=*/0);
}

void DwarfMetadataFetcher::TypeData::Dump(std::ostream &out, int level) const {
  auto Indent = [](int n) -> std::string { return std::string(n * 4, ' '); };
  out << Indent(level) << "// level=" << level << ", size=" << size
      << ", data_type=" << DataTypeToStr(data_type)
      << ", typedef_type.size()=" << typedef_type.size()
      << ", types.size()=" << types.size()
      << ", fields.size()=" << fields.size() << '\n';
  out << Indent(level) << DataTypeToShortString(data_type) << " "
      << (name.empty() ? "/*empty*/" : name);
  if (fields.empty() && types.empty() && typedef_type.empty() &&
      formal_parameters.empty()) {
    out << ";" << '\n';
    return;
  } else {
    out << " {" << '\n';
  }
  for (const auto &field : fields) {
    out << Indent(level + 1) << field->type_name << " " << field->name
        << "; // offset=" << field->offset << '\n';
  }
  for (const auto &p : formal_parameters) {
    out << Indent(level + 1) << "formal_param " << p << " " << p << ";" << '\n';
  }
  for (const auto &[name, const_value] : constant_variables) {
    out << Indent(level + 1) << name << ": " << const_value << ";" << '\n';
  }
  for (const auto &p : types) {
    p.second->Dump(out, level + 1);
  }
  for (const auto &p : typedef_type) {
    out << Indent(level + 1) << "typedef " << p.second << " " << p.first << ";"
        << '\n';
  }

  out << Indent(level + 1) << "};" << '\n';
}

void DwarfMetadataFetcher::TypeData::Debug(std::ostream &out, int level) const {
  auto Indent = [](int n) -> std::string { return std::string(n * 4, ' '); };
  out << Indent(level) << DataTypeToShortString(data_type) << ": " << name
      << "\n";
  for (const auto &p : types) {
    p.second->Debug(out, level + 1);
  }
}

void DwarfMetadataFetcher::TypeData::VisitChildDIE(
    const llvm::DWARFDie &die, bool should_read_subprogram,
    const ParseContext &context) {
  const llvm::dwarf::Tag die_tag = die.getTag();
  switch (die_tag) {
  case llvm::dwarf::DW_TAG_namespace:
  case llvm::dwarf::DW_TAG_class_type:
  case llvm::dwarf::DW_TAG_structure_type:
  case llvm::dwarf::DW_TAG_base_type:
  case llvm::dwarf::DW_TAG_array_type:
  case llvm::dwarf::DW_TAG_pointer_type:
  case llvm::dwarf::DW_TAG_ptr_to_member_type:
  case llvm::dwarf::DW_TAG_reference_type:
  case llvm::dwarf::DW_TAG_rvalue_reference_type:
  case llvm::dwarf::DW_TAG_enumeration_type:
  case llvm::dwarf::DW_TAG_union_type: {
    std::string child_name;
    std::optional<std::string> signature_type_name =
        ResolveSignature(die, context);
    if (signature_type_name) {
      child_name = *signature_type_name;
    } else {
      child_name = RecursiveGetNameOrResolveAnon(die);
    }
    if (child_name.empty()) {
      LOG(ERROR) << "child_name is empty for die: \n";
      die.dump();
    }
    if (!types.contains(child_name)) {
      AddType(child_name, std::make_unique<TypeData>());
    }
    types[child_name]->ParseDIE(die, should_read_subprogram, context);
    break;
  }
  case llvm::dwarf::DW_TAG_subprogram: {
    if (!should_read_subprogram) {
      break;
    }
    auto child_name = die.getLinkageName();
    if (child_name == nullptr) {
      // This is important if an allocation is made in 'main' for heapalloc
      // dwarf. This is because main does not have a
      // linkage name.
      child_name = die.getShortName();
      if (child_name == nullptr) {
        break;
      }
    }
    if (!types.contains(child_name)) {
      types[child_name] = std::make_unique<TypeData>();
    }
    types[child_name]->ParseDIE(die, should_read_subprogram, context);
    break;
  }
  case llvm::dwarf::DW_TAG_GOOGLE_heapalloc: {
    llvm::DWARFDie type_die =
        die.getAttributeValueAsReferencedDie(llvm::dwarf::DW_AT_type);

    type_die = RecursiveGetTypedefDIE(type_die);
    if (!die.isValid()) {
      break;
    }
    std::string type_name = GetTypeQualifiedName(type_die);
    uint64_t line_offset = die.getDeclLine();
    uint64_t col_number =
        llvm::dwarf::toUnsigned(die.find(llvm::dwarf::DW_AT_decl_column), 0);
    std::string func_name;
    if (die.find(llvm::dwarf::DW_AT_name)) {
      func_name = die.getShortName();
    } else {
      func_name = "";
    }
    heapalloc_sites.insert(
        {Frame(func_name, line_offset, col_number), type_name});
    break;
  }
  case llvm::dwarf::DW_TAG_typedef: {
    std::string name = RecursiveGetName(die);
    // Find the original canonical type
    llvm::DWARFDie cur = die;
    while (cur.isValid() && cur.getTag() == llvm::dwarf::DW_TAG_typedef) {
      cur = cur.getAttributeValueAsReferencedDie(llvm::dwarf::DW_AT_type);
    }
    if (cur.isValid()) {
      typedef_type[name] = GetTypeQualifiedName(cur);
    }
    break;
  }
  case llvm::dwarf::DW_TAG_member:
  case llvm::dwarf::DW_TAG_inheritance: {
    auto field = std::make_unique<FieldData>();
    field->ParseDIE(die, context);
    // Make sure we haven't already inserted somewhere else. This can happen
    // if we have multiple instances of the same type with different
    // instantiations.
    bool skip_field = false;
    for (const auto &f : fields) {
      if (field->offset == f->offset && field->type_name == f->type_name &&
          field->name == f->name) {
        skip_field = true;
      }
    }
    if (skip_field) {
      break;
    }
    auto opt = UnwrapParameterizedStorage(field->type_name);
    if (opt) {
      field->type_name = opt.value();
    }
    if (field->offset >= 0) {
      auto it = offset_idx.find(field->offset);
      if (it == offset_idx.end()) {
        absl::flat_hash_set<size_t> indices;
        indices.insert(fields.size());
        offset_idx[field->offset] = indices;
      } else {
        absl::flat_hash_set<size_t> *indices = &it->second;

        indices->insert(fields.size());
      }
      fields.push_back(std::move(field));
    }
    break;
  }
  // For now we treat both template and formal parameters the same. In
  // theory, they could be worth splitting up into separate cases.
  case llvm::dwarf::DW_TAG_template_type_parameter:
  case llvm::dwarf::DW_TAG_formal_parameter: {
    // For now we use the dumpTypeQualifiedName method. Recursing walking
    // through the dwarf DIEs does not lead to the correct typename.
    // RecursiveGetName will not always work here.
    auto formal_param_type = RecursiveGetTypeDIEFormalParam(die);
    llvm::DWARFDie unit_reference =
        formal_param_type.resolveTypeUnitReference();
    const std::string formal_param_name = GetTypeQualifiedName(unit_reference);
    if (formal_param_name.empty()) {
      LOG(ERROR) << "formal_param_name is empty for formal param: \n";
      die.dump();
    }
    if (std::find(formal_parameters.begin(), formal_parameters.end(),
                  formal_param_name) == formal_parameters.end()) {
      formal_parameters.push_back(formal_param_name);
    }
    break;
  }
  case llvm::dwarf::DW_TAG_template_value_parameter:
  case llvm::dwarf::DW_TAG_variable: {
    std::optional<llvm::DWARFFormValue> const_value =
        die.find(llvm::dwarf::DW_AT_const_value);
    if (!const_value) {
      break;
    }
    const char *name = die.getShortName();
    if (name == nullptr) {
      break;
    }
    uint64_t const_value_as_unsigned = llvm::dwarf::toUnsigned(const_value, 0);
    constant_variables.insert({name, const_value_as_unsigned});
    break;
  }
  default:
    break;
  }
}

void DwarfMetadataFetcher::TypeData::ParseDIE(const llvm::DWARFDie &die,
                                              bool should_read_subprogram,
                                              const ParseContext &context) {
  const llvm::dwarf::Tag die_tag = die.getTag();
  switch (die_tag) {
  case llvm::dwarf::DW_TAG_namespace: {
    data_type = DataType::NAMESPACE;
    break;
  }
  case llvm::dwarf::DW_TAG_class_type: {
    data_type = DataType::CLASS;
    break;
  }
  case llvm::dwarf::DW_TAG_enumeration_type: {
    data_type = DataType::ENUM;
    break;
  }
  case llvm::dwarf::DW_TAG_structure_type: {
    data_type = DataType::STRUCTURE;
    break;
  }
  case llvm::dwarf::DW_TAG_base_type: {
    data_type = DataType::BASE_TYPE;
    break;
  }
  case llvm::dwarf::DW_TAG_array_type:
  case llvm::dwarf::DW_TAG_pointer_type:
  case llvm::dwarf::DW_TAG_ptr_to_member_type:
  case llvm::dwarf::DW_TAG_reference_type:
  case llvm::dwarf::DW_TAG_rvalue_reference_type: {
    data_type = DataType::POINTER_LIKE;
    break;
  }
  case llvm::dwarf::DW_TAG_subprogram: {
    data_type = DataType::SUBPROGRAM;
    const char *linkage_name = die.getLinkageName();
    if (linkage_name != nullptr) {
      name = linkage_name;
    }
    break;
  }
  case llvm::dwarf::DW_TAG_union_type: {
    data_type = DataType::UNION;
    break;
  }
  default: {
    data_type = DataType::UNKNOWN;
    break;
  }
  }
  if (data_type == DataType::BASE_TYPE || data_type == DataType::CLASS ||
      data_type == DataType::STRUCTURE || data_type == DataType::UNION ||
      data_type == DataType::ENUM) {
    std::optional<llvm::DWARFFormValue> optional_value =
        die.find(llvm::dwarf::DW_AT_byte_size);
    if (optional_value) {
      this->size = optional_value.value().getRawUValue();
    }
  }
  // TODO: b/350771311 - Keep it simple for now and stop if we are at a union
  // type. Notice that we are not visiting children of a union type.
  if (data_type == DataType::NAMESPACE || data_type == DataType::CLASS ||
      data_type == DataType::STRUCTURE || data_type == DataType::SUBPROGRAM ||
      data_type == DataType::UNION) {
    llvm::DWARFDie child_die = die.getFirstChild();
    while (child_die) {
      VisitChildDIE(child_die, should_read_subprogram, context);
      child_die = child_die.getSibling();
    }
  }
}

std::vector<absl::string_view>
DwarfMetadataFetcher::SplitNamespace(absl::string_view type_name) {
  if (type_name.empty()) {
    return {};
  }
  std::vector<absl::string_view> names;
  int i = 0, prev = 0, stack = 0;
  for (; i < type_name.size() - 1; i++) {
    if (type_name[i] == '<') {
      stack++;
    } else if (type_name[i] == '>') {
      stack--;
    } else if (type_name.substr(i, 2) == "::") {
      if (stack == 0) {
        names.push_back(type_name.substr(prev, i - prev));
        prev = i + 2;
        i++;
      }
    }
  }
  names.push_back(type_name.substr(prev, type_name.size() - prev));
  return names;
}

absl::StatusOr<const DwarfMetadataFetcher::FieldData *>
DwarfMetadataFetcher::GetField(absl::string_view type_name,
                               int64_t offset) const {
  auto type_data_or_err = GetType(type_name);
  if (!type_data_or_err.ok()) {
    return type_data_or_err.status();
  }
  const TypeData *type_data = type_data_or_err.value();
  if (offset < 0 || offset >= type_data->size) {
    return absl::InvalidArgumentError(
        absl::StrCat("Invalid offset value: ", offset));
  }
  if (type_data->fields.empty() || type_data->offset_idx.empty()) {
    return absl::NotFoundError("No field in this type");
  }
  auto it = type_data->offset_idx.upper_bound(offset);
  if (it == type_data->offset_idx.begin()) {
    return absl::NotFoundError("No such field");
  }
  it--;
  if (it->second.size() > 1) {
    return absl::NotFoundError(
        absl::StrCat("Multiple fields with offset ", offset));
  }
  return type_data->fields[*(it->second.begin())].get();
}

absl::StatusOr<const DwarfMetadataFetcher::TypeData *>
DwarfMetadataFetcher::SearchType(
    const DwarfMetadataFetcher::TypeData *parent_type,
    const std::vector<absl::string_view> &names, int cur) const {
  absl::string_view cur_name = names[cur];

  // If the current name is the anonymous namespace, then we do a greedy search
  // for any subtype that has prefix Anon and is namespace. This is hacky way to
  // solve the new way of dealing with namespaces by giving anonymous types
  // actual names. In theory, this 'could' cause conflicts, but this would
  // require very terrible naming?
  if (cur_name == "(anonymous namespace)") {
    for (auto it = parent_type->types.begin(); it != parent_type->types.end();
         ++it) {
      if (absl::StartsWith(it->first, "Anon") &&
          it->second->data_type == DataType::NAMESPACE) {
        auto type_or = SearchType(it->second.get(), names, cur + 1);
        if (type_or.status().ok()) {
          return type_or;
        }
      }
    }
    return absl::NotFoundError(absl::StrCat(
        "type not found, stuck in anonymous namespace: ", MergeNames(names)));
  }

  // If we find a typedef, we need to start over searching from the root type
  // space. This is because the type referred to by a typedef can be in a
  // completely different namespace hierarchy.
  if (parent_type->typedef_type.contains(cur_name)) {
    cur_name = parent_type->typedef_type.at(cur_name);
    return GetType(cur_name);
  }

  // If it is the last item, i.e. the short type_name without namespaces,
  // then search the parent_type's sub types, returns not found if not
  // match.
  if (cur == names.size() - 1) {
    if (parent_type->types.contains(cur_name)) {
      return parent_type->types.at(cur_name).get();
    }
    return absl::NotFoundError(
        absl::StrCat("type not found: ", MergeNames(names)));
  }
  // Reaching here means cur_name is not the short type_name but a namespace.
  // If parent_type has a sub type/namespace that matches the current
  // namespace name, then search it.
  if (parent_type->types.contains(cur_name)) {
    auto type_or =
        SearchType(parent_type->types.at(cur_name).get(), names, cur + 1);
    if (type_or.status().ok()) {
      return type_or;
    }
  }
  // If no match so far, then it is possible that the target type falls
  // into the "empty-name-parent-type", so search the type with empty
  // name.
  if (parent_type->types.contains("")) {
    return SearchType(parent_type->types.at("").get(), names, cur + 1);
  }
  return absl::NotFoundError(
      absl::StrCat("type not found: ", MergeNames(names)));
}

absl::StatusOr<const DwarfMetadataFetcher::TypeData *>
DwarfMetadataFetcher::GetType(absl::string_view type_name) const {
  if (type_name.empty()) {
    return absl::InvalidArgumentError("type_name cannot be empty.");
  }
  auto names = SplitNamespace(type_name);
  auto type_or = SearchType(pack_.root_space.get(), names, 0);
  if (type_or.status().ok()) {
    if (type_or.value()->data_type == DataType::NAMESPACE) {
      return absl::InvalidArgumentError(absl::StrCat(
          "type_name ", type_name, " refers to a non-type namespace."));
    }
  }
  return type_or;
}

absl::StatusOr<const DwarfMetadataFetcher::TypeData *>
DwarfMetadataFetcher::GetCacheableType(absl::string_view type_name) {
  if (cache_.contains(type_name)) {
    return cache_.at(type_name);
  } else {
    auto type_data_or_err = GetType(type_name);
    if (!type_data_or_err.ok()) {
      return type_data_or_err.status();
    }
    const TypeData *type_data = type_data_or_err.value();
    cache_.insert({std::string(type_name), type_data});
    return type_data;
  }
}

absl::StatusOr<std::string>
DwarfMetadataFetcher::GetHeapAllocType(const Frame &frame) const {
  auto it = pack_.heapalloc_sites.find(frame);
  if (it == pack_.heapalloc_sites.end()) {
    return absl::NotFoundError(absl::StrCat(
        "No HeapAllocSite data for frame with func: ", frame.function_name,
        " at line ", frame.line_offset, " with column ", frame.column));
  }
  return it->second;
}

absl::StatusOr<std::vector<std::string>>
DwarfMetadataFetcher::GetFormalParameters(
    absl::string_view linkage_name) const {
  if (!pack_.formal_and_template_param_map.contains(linkage_name)) {
    return absl::NotFoundError(
        absl::StrCat("No Subprogram data for ", linkage_name));
  } else {
    return pack_.formal_and_template_param_map.at(linkage_name);
  }
}

std::unique_ptr<llvm::DWARFUnit>
DwarfMetadataFetcher::DwarfParserState::getNextDwarfUnit() {
  m_.Lock();
  switch (curr_state_) {
  case State::kTypesSectionUnits:
    if (curr_it_ == dwarf_info_->types_section_units().begin()) {
      LOG(INFO) << "starting parsing binary file" << "\n";
    }
    if (curr_it_ == dwarf_info_->types_section_units().end()) {
      curr_it_ = dwarf_info_->info_section_units().begin();
      curr_state_ = State::kInfoSectionUnits;
      m_.Unlock();
      return getNextDwarfUnit();
    }
    break;
  case State::kInfoSectionUnits:
    if (curr_it_ == dwarf_info_->info_section_units().end()) {
      if (dwp_dwarf_info_ != nullptr) {
        curr_it_ = dwp_dwarf_info_->dwo_types_section_units().begin();
        curr_state_ = State::kDwpTypesSectionUnits;
        LOG(INFO) << "starting parsing dwp file" << "\n";
        m_.Unlock();
        return getNextDwarfUnit();
      } else {
        m_.Unlock();
        return nullptr;
      }
    }
    break;
  case State::kDwpTypesSectionUnits:
    if (curr_it_ == dwp_dwarf_info_->dwo_types_section_units().end()) {
      curr_it_ = dwp_dwarf_info_->dwo_info_section_units().begin();
      curr_state_ = State::kDwpInfoSectionUnits;
      m_.Unlock();
      return getNextDwarfUnit();
    }
    break;
  case State::kDwpInfoSectionUnits:
    if (curr_it_ == dwp_dwarf_info_->dwo_info_section_units().end()) {
      m_.Unlock();
      return nullptr;
    }
    break;
  }
  std::unique_ptr<llvm::DWARFUnit> next = std::move(*curr_it_);
  curr_it_++;

  next->dump(llvm::outs(), llvm::DIDumpOptions::getForSingleDIE());
  llvm::outs() << "\n";
  m_.Unlock();
  return next;
}

bool DwarfMetadataFetcher::MetadataPack::Empty() const {
  return root_space->types.empty() && root_space->typedef_type.empty();
}

absl::Status DwarfMetadataFetcher::MetadataPack::Insert(MetadataPack &other) {
  if (other.Empty()) {
    return absl::OkStatus();
  }
  if (pointer_size != 0 && pointer_size != other.pointer_size) {
    return absl::InternalError("Pointer size inconsistent");
  }
  pointer_size = other.pointer_size;
  for (auto &p : other.root_space->types) {
    if (!root_space->types.contains(p.first)) {
      root_space->types[p.first] = std::move(p.second);
    }
  }
  root_space->typedef_type.insert(other.root_space->typedef_type.begin(),
                                  other.root_space->typedef_type.end());
  formal_and_template_param_map.insert(
      other.formal_and_template_param_map.begin(),
      other.formal_and_template_param_map.end());
  heapalloc_sites.merge(other.heapalloc_sites);
  return absl::OkStatus();
}

std::ostream &operator<<(std::ostream &out,
                         const DwarfMetadataFetcher::Frame &frame) {
  out << frame.function_name << ": " << frame.line_offset << ": "
      << frame.column;
  return out;
}
