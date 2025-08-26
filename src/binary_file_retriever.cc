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



#include "binary_file_retriever.h"

#include <fstream>
#include <memory>

#include "absl/log/log.h"
#include "absl/status/statusor.h"

absl::StatusOr<std::unique_ptr<BinaryFileRetriever>>
BinaryFileRetriever::CreateBinaryFileRetriever() {
  return std::make_unique<BinaryFileRetriever>();
}

std::unique_ptr<BinaryFileRetriever> BinaryFileRetriever::CreateMockRetriever(
    const absl::flat_hash_map<std::string, std::string> &test_modules_map) {
  return std::make_unique<BinaryFileRetriever>();
}

bool BinaryFileRetriever::CheckExists(const std::string &stored_path) const {
  std::ifstream file_check(stored_path);
  if (file_check.good()) {
    VLOG(2) << "Path |" << stored_path << "| exists.";
    return true;
  }

  VLOG(2) << "Path |" << stored_path
          << "| does not exist or is not accessible.";
  return false;
}

absl::StatusOr<std::string> BinaryFileRetriever::RetrieveBinary(
    const std::string &build_id, const std::string &stored_path) const {
  return RetrieveFile(stored_path);
}

absl::StatusOr<std::string> BinaryFileRetriever::RetrieveDwpFile(
    const std::string &stored_path) const {
  return RetrieveFile(stored_path);
}

absl::StatusOr<std::string> BinaryFileRetriever::RetrieveFile(
    const std::string &stored_path) const {
  if (CheckExists(stored_path)) {
    return stored_path;
  } else {
    return absl::NotFoundError("Binary File Not found!");
  }
}
