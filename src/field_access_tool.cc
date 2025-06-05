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

#include <cstdint>
#include <functional>
#include <iostream>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "absl/flags/flag.h"
#include "absl/flags/parse.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "histogram_builder.h"
#include "status_macros.h"

ABSL_FLAG(bool, local, false, "Collect data from local heap profile");
ABSL_FLAG(std::string, out, "", "Output file path, defaults to stdout");
ABSL_FLAG(bool, stats, false,
          "Log stats about the type resolution and histogram building.");
ABSL_FLAG(bool, verify_verbose, false,
          "Verify type trees and print out verbose information.");
ABSL_FLAG(std::vector<std::string>, type_prefix_filter, {},
          "List of types to filter on. If empty, will choose all types.");
ABSL_FLAG(bool, only_records, false,
          "This flag ensures everything in the histogram is an object.");
ABSL_FLAG(std::vector<std::string>, callstack_filter, {},
          "List of callstack mangled function names to filter on. If empty, "
          "will choose all callstacks.");
ABSL_FLAG(bool, flamegraph, false, "Dump flamegraph of the type tree.");
ABSL_FLAG(int64_t, limit, -1,
          "Limit on the number of type trees to dump. If negative, dump all.");
ABSL_FLAG(bool, dump_unresolved_callstacks, false,
          "Flag for debugging. Dumps callstacks that are not resolved instead "
          "of resolved type trees.");
ABSL_FLAG(uint32_t, parse_thread_count, 128,
          "Number of threads to use for parsing DWARF files.");

// Local mode flags.
ABSL_FLAG(std::string, memprof_profile, "",
          "The local path for a raw MemProf profile.");
ABSL_FLAG(std::string, memprof_profiled_binary, "",
          "The local path for the MemProf profiled binary.");
ABSL_FLAG(std::string, memprof_profiled_binary_dwarf, "",
          "The local path for the dwarf file of the profiled binary. This "
          "options is only used if dwarf file is split from the binary, "
          "otherwise is set to memprof_profiled_binary.");

namespace {
using devtools_crosstool_fdo_field_access::AbstractHistogramBuilder;
using devtools_crosstool_fdo_field_access::HistogramBuilderResults;
using devtools_crosstool_fdo_field_access::LocalHistogramBuilder;
using devtools_crosstool_fdo_field_access::TypeTreeStore;

absl::StatusOr<std::unique_ptr<AbstractHistogramBuilder>>
CreateLocalHistogramBuilderFromFlags() {
  std::string memprof_profiled_binary =
      absl::GetFlag(FLAGS_memprof_profiled_binary);
  QCHECK(!memprof_profiled_binary.empty())
      << "Profiled binary must be specified if with --local mode.";
  std::string memprof_profile = absl::GetFlag(FLAGS_memprof_profile);
  QCHECK(!memprof_profile.empty())
      << "Memprofraw profile must be specified if with --local mode.";

  std::string memprof_profiled_binary_dwarf =
      absl::GetFlag(FLAGS_memprof_profiled_binary_dwarf);

  std::vector<std::string> type_prefix_filter =
      absl::GetFlag(FLAGS_type_prefix_filter);

  std::vector<std::string> callstack_filter =
      absl::GetFlag(FLAGS_callstack_filter);

  bool verify_verbose = absl::GetFlag(FLAGS_verify_verbose);

  bool only_records = absl::GetFlag(FLAGS_only_records);

  uint32_t parse_thread_count = absl::GetFlag(FLAGS_parse_thread_count);

  bool dump_unresolved_callstacks =
      absl::GetFlag(FLAGS_dump_unresolved_callstacks);
  if (memprof_profiled_binary_dwarf.empty()) {
    LOG(INFO) << "Setting local .dwp file to " << memprof_profiled_binary
              << "\n";
    memprof_profiled_binary_dwarf = memprof_profiled_binary;
  }
  return LocalHistogramBuilder::Create(
      memprof_profile, memprof_profiled_binary, memprof_profiled_binary_dwarf,
      type_prefix_filter, callstack_filter, only_records, verify_verbose,
      dump_unresolved_callstacks, parse_thread_count);
}

absl::StatusOr<std::unique_ptr<HistogramBuilderResults>> LocalMode() {
  ASSIGN_OR_RETURN(std::unique_ptr<AbstractHistogramBuilder> histogram_builder,
                   CreateLocalHistogramBuilderFromFlags());
  ASSIGN_OR_RETURN(
      std::unique_ptr<HistogramBuilderResults> histogram_builder_results,
      histogram_builder->BuildHistogram());
  return std::move(histogram_builder_results);
};

}  // namespace

int main(int argc, char* argv[]) {
  absl::ParseCommandLine(argc, argv);
  const bool local = absl::GetFlag(FLAGS_local);
  const bool stats = absl::GetFlag(FLAGS_stats);
  const bool dump_unresolved_callstacks =
      absl::GetFlag(FLAGS_dump_unresolved_callstacks);
  const int64_t limit = absl::GetFlag(FLAGS_limit);
  const std::string out = absl::GetFlag(FLAGS_out);
  absl::StatusOr<std::unique_ptr<HistogramBuilderResults>>
      histogram_builder_results;
  if (local) {
    LOG(INFO) << "Running field access tool in local mode.\n";
    histogram_builder_results = LocalMode();
  } else {
    LOG(ERROR) << "Must choose local mode for field_access_tool.";
    return 1;
  }

  if (!histogram_builder_results.ok()) {
    LOG(ERROR) << "Failed to build histogram: "
               << histogram_builder_results.status();
    return 1;
  }

  std::function<void(std::ostream&)> dump_fn;
  if (dump_unresolved_callstacks) {
    // do nothing.
  } else if (absl::GetFlag(FLAGS_flamegraph)) {
    dump_fn = [&](std::ostream& out) {
      histogram_builder_results.value()->type_tree_store->DumpFlamegraph(out,
                                                                         limit);
    };
  } else {
    auto dump_fn = [&](std::ostream& out) {
      histogram_builder_results.value()->type_tree_store->Dump(out, limit);
    };
    dump_fn(std::cout);
  }

  if (stats) {
    histogram_builder_results.value()->stats.Log();
  }
  return 0;
}
