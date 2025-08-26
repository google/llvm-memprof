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

#include <sys/types.h>

#include <cstddef>
#include <cstdint>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/string_view.h"
#include "absl/time/civil_time.h"
#include "absl/time/time.h"
#include "absl/types/span.h"
#include "binary_file_retriever.h"
#include "dwarf_metadata_fetcher.h"
#include "llvm/include/llvm/ADT/StringExtras.h"
#include "llvm/include/llvm/Demangle/Demangle.h"
#include "llvm/include/llvm/Object/Binary.h"
#include "llvm/include/llvm/Object/BuildID.h"
#include "llvm/include/llvm/Object/ObjectFile.h"
#include "llvm/include/llvm/ProfileData/MemProf.h"
#include "llvm/include/llvm/ProfileData/MemProfReader.h"
#include "llvm/include/llvm/Support/Error.h"
#include "llvm/include/llvm/Support/ErrorOr.h"
#include "llvm/include/llvm/Support/MemoryBuffer.h"
#include "src/object_layout.pb.h"
#include "status_macros.h"
#include "type_resolver.h"
#include "type_tree.h"

namespace devtools_crosstool_fdo_field_access {

using llvm::memprof::RawMemProfReader;

constexpr char kCacheDir[] = "/tmp/dwarf_metadata";

namespace {

double Percentify(uint64_t value, uint64_t total) {
  return 100.0 * ((double)value / (double)total);
}
}  // namespace

void Statistics::Log() const {
  LOG(INFO) << "- \n"
            << " ====== Statistics ======\n"
            << "Total allocations count: " << total_allocations_count << "("
            << Percentify(total_allocations_count, total_allocations_count)
            << "%)\n"
            << "Total found type: " << total_found_type << "("
            << Percentify(total_found_type, total_allocations_count) << "%)\n"
            << "Total duplicate callstack: " << duplicate_callstack_count << "("
            << Percentify(duplicate_callstack_count, total_allocations_count)
            << "%)\n"
            << "Total verified: " << total_verified << "("
            << Percentify(total_verified, total_allocations_count) << "%)\n"
            << "Heap alloc count: " << heap_alloc_count << "("
            << Percentify(heap_alloc_count, total_allocations_count) << "%)\n"
            << "Container alloc count: " << container_alloc_count << "("
            << Percentify(container_alloc_count, total_allocations_count)
            << "%)\n"
            << "Total record count: " << total_record_count << "("
            << Percentify(total_record_count, total_allocations_count) << "%)\n"
            << "Total after filtering: " << total_after_filtering << "("
            << Percentify(total_after_filtering, total_allocations_count)
            << "%)\n"
            << "Total accesses: " << total_accesses << "("
            << Percentify(total_accesses, total_accesses) << "%)\n"
            << "Total accesses on heapallocs: " << total_accesses_on_heapallocs
            << "(" << Percentify(total_accesses_on_heapallocs, total_accesses)
            << "%)\n"
            << "Total accesses on containers: " << total_accesses_on_containers
            << "(" << Percentify(total_accesses_on_containers, total_accesses)
            << "%)\n"
            << "Total accesses on records: " << total_accesses_on_records << "("
            << Percentify(total_accesses_on_records, total_accesses) << "%)\n"
            << " ======    End    ======\n";
}

// Use command line to get build ID for local files. This is QoL to avoid
// manually looking up the buildID each time we run the in local mode.
absl::StatusOr<std::string> GetBuildIdForLocalFile(
    absl::string_view memprof_profiled_binary) {
  llvm::Expected<llvm::object::OwningBinary<llvm::object::ObjectFile>> elfobj =
      llvm::object::ObjectFile::createObjectFile(memprof_profiled_binary);
  if (!elfobj) {
    return absl::InvalidArgumentError(absl::StrCat(
        "Cannot create object file for ", memprof_profiled_binary));
  }
  return llvm::toHex(::llvm::object::getBuildID(elfobj->getBinary()),
                     /*lowercase=*/true);
}  // namespace

void LogCallStackAndTypeTree(const TypeTreeStore::CallStack& callstack,
                             const TypeTree* type_tree, bool verify_verbose) {
  if (verify_verbose) {
    std::stringstream error_string;
    if (type_tree) {
      type_tree->Dump(error_string);
      error_string << "\n";
    } else {
      error_string << "- \n";
    }
    TypeTreeStore::DumpCallStack(callstack, error_string);
    error_string.flush();
    LOG(WARNING) << error_string.str();
  }
}

absl::StatusOr<std::unique_ptr<HistogramBuilderResults>>
LocalHistogramBuilder::BuildHistogram() {
  Statistics stats;
  auto type_tree_store = std::make_unique<TypeTreeStore>();
  for (const auto& [unused, record] : *memprof_reader_) {
    for (const llvm::memprof::AllocationInfo& alloc_info : record.AllocSites) {
      bool log = false;
      QCHECK(!alloc_info.CallStack.empty()) << "Empty callstack for allocation";
      TypeTreeStore::CallStack callstack =
          TypeTreeStore::ConvertCallStack(alloc_info.CallStack);
      if (FilterCallstack(callstack)) {
        continue;
      }
      stats.total_allocations_count++;

      auto status_or_type_tree = dwarf_type_resolver_->ResolveTypeFromCallstack(
          callstack, alloc_info.Info.getAccessHistogramSize() * 8);

      if (!status_or_type_tree.ok()) {
        if (verify_verbose_) {
          LOG(WARNING) << "Failed to resolve type from callstack: \n"
                       << status_or_type_tree.status();
          LogCallStackAndTypeTree(callstack, nullptr, verify_verbose_);
        }
        if (dump_unresolved_callstacks_) {
          TypeTreeStore::DumpCallStack(callstack, std::cout, /*level=*/0,
                                       /*as_entry=*/true);
        }
        continue;
      }
      stats.total_found_type++;

      std::unique_ptr<TypeTree> type_tree =
          std::move(status_or_type_tree.value());

      if (FilterType(type_tree->Name())) {
        continue;
      }

      stats.total_after_filtering++;

      if (type_tree->IsRecordType()) {
        stats.total_record_count++;
      }

      if (only_records_ && !type_tree->IsRecordType()) {
        continue;
      }

      absl::Status status = type_tree->RecordAccessHistogram<
          kMemprofHistogramGranularity,
          TypeTree::AccessCounters::AccessType::kAccess>(
          (uint64_t*)alloc_info.Info.getAccessHistogram(),
          alloc_info.Info.getAccessHistogramSize());
      if (!status.ok()) {
        log = true;
        if (verify_verbose_) {
          LOG(WARNING) << "Collapsing histogram does not precisely align with "
                          "type size, counters may be distorted for: \n";
        }
      }

      if (!type_tree->Verify(verify_verbose_)) {
        LogCallStackAndTypeTree(callstack, type_tree.get(), verify_verbose_);
      }

      stats.total_verified++;

      stats.total_accesses += type_tree->Root()->GetTotalAccessCount();

      if (type_tree->FromContainer()) {
        stats.container_alloc_count++;
        stats.total_accesses_on_containers +=
            type_tree->Root()->GetTotalAccessCount();
      } else {
        stats.heap_alloc_count++;
        stats.total_accesses_on_heapallocs +=
            type_tree->Root()->GetTotalAccessCount();
      }

      if (type_tree->IsRecordType()) {
        stats.total_accesses_on_records +=
            type_tree->Root()->GetTotalAccessCount();
      }

      if (log) {
        LogCallStackAndTypeTree(callstack, type_tree.get(), verify_verbose_);
      }
      RETURN_IF_ERROR(
          type_tree_store->Insert(alloc_info.CallStack, std::move(type_tree)));
    }
  }

  return std::make_unique<HistogramBuilderResults>(std::move(type_tree_store),
                                                   stats);
}

void TypeTreeStore::Dump(std::ostream& os, int64_t limit) const {
  // If negative, print all.
  int64_t N = limit < 0 ? callstack_to_type_tree_.size() : limit;
  int64_t i = 0;
  for (const auto& [callstack, type_tree] : callstack_to_type_tree_) {
    if (i >= N) {
      return;
    }
    os << "- Entry: \n";
    os << "    type_tree: \n";
    type_tree->Dump(os, 3);
    os << "    callstack: \n";
    DumpCallStack(callstack, os, 3);
    i++;
  }
}

void TypeTreeStore::DumpFlamegraph(std::ostream& os, int64_t limit) const {
  // If negative, print all.
  int64_t N = limit < 0 ? callstack_to_type_tree_.size() : limit;
  int64_t i = 0;
  for (const auto& [unused, type_tree] : callstack_to_type_tree_) {
    if (i >= N) {
      return;
    }
    type_tree->DumpFlameGraph(os, i + 1);
    i++;
  }
}

TypeTreeStore::CallStack TypeTreeStore::ConvertCallStack(
    absl::Span<const llvm::memprof::Frame> callstack) {
  std::vector<DwarfMetadataFetcher::Frame> dwarf_callstack;
  dwarf_callstack.reserve(callstack.size());
  for (const auto& frame : callstack) {
    dwarf_callstack.push_back(DwarfMetadataFetcher::Frame(
        frame.hasSymbolName() ? frame.getSymbolName().str() : "<none>",
        frame.LineOffset, frame.Column));
  }
  return dwarf_callstack;
}

absl::StatusOr<const TypeTree*> TypeTreeStore::InsertAndGet(
    const CallStack& callstack,
    std::unique_ptr<devtools_crosstool_fdo_field_access::TypeTree> type_tree) {
  RETURN_IF_ERROR(Insert(callstack, std::move(type_tree)));
  return callstack_to_type_tree_[callstack].get();
}

absl::Status TypeTreeStore::Insert(
    const CallStack& callstack,
    std::unique_ptr<devtools_crosstool_fdo_field_access::TypeTree> type_tree) {
  if (!type_tree) {
    return absl::InvalidArgumentError("TypeTree is null.");
  }
  auto it = callstack_to_type_tree_.find(callstack);
  if (it != callstack_to_type_tree_.end()) {
    const TypeTree* curr_type_tree = it->second.get();
    if (curr_type_tree->Name() != type_tree->Name()) {
      return absl::InvalidArgumentError(absl::StrCat(
          "Trying to insert different type trees for the same callstack",
          curr_type_tree->Name(), " vs ", type_tree->Name()));
    }
    RETURN_IF_ERROR(type_tree->MergeCounts(curr_type_tree));
  }
  callstack_to_type_tree_[callstack] = std::move(type_tree);
  return absl::OkStatus();
}

std::vector<TypeTreeStore::CallStack> TypeTreeStore::GetCallStacksForTypeName(
    std::string root_type_name) const {
  std::vector<CallStack> callstacks;
  for (const auto& [callstack, type_tree] : callstack_to_type_tree_) {
    if (type_tree->Name() == root_type_name) {
      callstacks.push_back(callstack);
    }
  }
  return callstacks;
}

absl::StatusOr<std::shared_ptr<TypeTree>> TypeTreeStore::GetTypeTree(
    const std::vector<DwarfMetadataFetcher::Frame>& callstack) const {
  auto it = callstack_to_type_tree_.find(callstack);
  if (it != callstack_to_type_tree_.end()) {
    return it->second;
  }
  return absl::NotFoundError("TypeTree not found for callstack.");
}

bool LocalHistogramBuilder::FilterType(absl::string_view type_name) const {
  if (type_prefix_filter_.empty()) {
    return false;
  }
  for (const auto& type : type_prefix_filter_) {
    if (absl::StartsWith(type_name, type)) {
      return false;
    }
  }
  return true;
}

bool LocalHistogramBuilder::FilterCallstack(
    const TypeTreeStore::CallStack& callstack) const {
  if (callstack_filter_.empty()) {
    return false;
  }
  for (const auto& frame : callstack) {
    for (const auto& callstack_filter : callstack_filter_) {
      if (frame.function_name == callstack_filter) {
        return false;
      }
    }
  }
  return true;
}

absl::StatusOr<std::unique_ptr<AbstractHistogramBuilder>>
LocalHistogramBuilder::Create(
    std::string memprof_profile, std::string memprof_profiled_binary,
    std::string memprof_profiled_binary_dwarf,
    const std::vector<std::string>& type_prefix_filter,
    const std::vector<std::string>& callstack_filter, bool only_records,
    bool verify_verbose, bool dump_unresolved_callstacks,
    uint32_t parse_thread_count) {
  std::string build_id;
  auto status_or = GetBuildIdForLocalFile(memprof_profiled_binary);
  if (status_or.ok()) {
    build_id = status_or.value();
  } else {
    build_id = "";
    LOG(WARNING) << "Failed to get build id for local file: "
                 << status_or.status() << " continuing with empty build id.";
  }

  llvm::ErrorOr<std::unique_ptr<llvm::MemoryBuffer>> buffer_or_error =
      llvm::MemoryBuffer::getFile(memprof_profile);
  if (auto ec = buffer_or_error.getError()) {
    return absl::InternalError(absl::StrFormat(
        "Error opening profile file `%s`:%s", memprof_profile, ec.message()));
  }
  llvm::Expected<std::unique_ptr<RawMemProfReader>> rawmemprof_reader =
      llvm::memprof::RawMemProfReader::create(std::move(buffer_or_error.get()),
                                              memprof_profiled_binary,
                                              /*KeepName=*/true);
  if (llvm::Error error = rawmemprof_reader.takeError()) {
    return absl::InternalError(absl::StrFormat(
        "Could not create reader: %s", llvm::toString(std::move(error))));
  }

  // Create BinaryFileRetriever that tries to lookup the dwarf binary in
  // the symbol server. Since we are running on local file, it will not
  // find it, and just look at the binary file only instead. If we do not
  // initialize stubs, the DwarfMetadataFetcher will crash. Changing this
  // requires some more overhaul of the DwarfMetadataFetcher class and the
  // way it looks for .dwp files.
  ASSIGN_OR_RETURN(std::unique_ptr<BinaryFileRetriever> binary_file_retriever,
                   BinaryFileRetriever::CreateBinaryFileRetriever());

  auto dwarf_metadata_fetcher = std::make_unique<DwarfMetadataFetcher>(
      std::move(binary_file_retriever), kCacheDir,
      /*should_read_subprogram=*/true, parse_thread_count);

  LOG(INFO) << "Fetching DWP with path: " << memprof_profiled_binary_dwarf
            << " for build id: " << build_id << "\n";
  // Read the dwarf file into the cache before we pass to type_resolver.
  RETURN_IF_ERROR(dwarf_metadata_fetcher->FetchDWPWithPath(
      {{.build_id = build_id, .path = memprof_profiled_binary_dwarf}},
      /*force_update_cache=*/true));
      
  auto type_resolver = std::make_unique<DwarfTypeResolver>(
      std::move(dwarf_metadata_fetcher), /*is_local=*/true);

  return std::make_unique<LocalHistogramBuilder>(
      std::move(rawmemprof_reader.get()), std::move(type_resolver),
      type_prefix_filter, callstack_filter, only_records, verify_verbose,
      dump_unresolved_callstacks);
}

void TypeTreeStore::DumpCallStack(const CallStack& callstack, std::ostream& os,
                                  int level, bool as_entry) {
  if (as_entry) {
    os << "- entry: \n";
    level += 2;
  }
  std::string indent = "";
  for (size_t i = 0; i < level; ++i) {
    indent += "  ";
  }
  for (const auto& frame : callstack) {
    os << indent << "- function_name: " << frame.function_name << "\n"
       << indent << "  line_offset: " << frame.line_offset << "\n"
       << indent << "  column: " << frame.column << "\n";
  }
}

}  // namespace devtools_crosstool_fdo_field_access
