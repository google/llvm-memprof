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

#ifndef BINARY_FILE_RETRIEVER_H_
#define BINARY_FILE_RETRIEVER_H_

#include <memory>
#include <string>

#include "absl/container/flat_hash_map.h"
#include "absl/status/statusor.h"

class BinaryFileRetriever final {
 public:
  static absl::StatusOr<std::unique_ptr<BinaryFileRetriever>>
  CreateBinaryFileRetriever();

  static std::unique_ptr<BinaryFileRetriever> CreateMockRetriever(
      const absl::flat_hash_map<std::string, std::string> &test_modules_map);

  absl::StatusOr<std::string> RetrieveBinary(
      const std::string &build_id, const std::string &stored_path) const;

  absl::StatusOr<std::string> RetrieveDwpFile(
      const std::string &build_id) const;

 private:
  bool CheckExists(const std::string &stored_path) const;

  absl::StatusOr<std::string> RetrieveFile(
      const std::string &stored_path) const;
};

#endif  // BINARY_FILE_RETRIEVER_H_