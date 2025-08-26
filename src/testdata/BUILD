# Copyright 2025 Google LLC
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

load("@rules_cc//cc:defs.bzl", "cc_binary", "cc_proto_library")
load("@rules_proto//proto:defs.bzl", "proto_library")

cc_binary(
    name = "proto_simple",
    srcs = ["proto_simple.cc"],
    deps = [":type_resolution_test_cc_proto"],
)

cc_binary(
    name = "proto_complex",
    srcs = ["proto_complex.cc"],
    deps = [":type_resolution_test_cc_proto"],
)

cc_binary(
    name = "heap_alloc",
    srcs = ["heap_alloc.cc"],
)

cc_proto_library(
    name = "type_resolution_test_cc_proto",
    deps = [":type_resolution_test_proto"],
)

proto_library(
    name = "type_resolution_test_proto",
    srcs = ["type_resolution_test.proto"],
)

cc_binary(
    name = "supported_adt_containers",
    srcs = ["supported_adt_containers.cc"],
    deps = ["@llvm-project//llvm:Support"],
)

cc_binary(
    name = "supported_abseil_containers",
    srcs = ["supported_abseil_containers.cc"],
    deps = [
        "@com_google_absl//absl/container",
        "@com_google_absl//absl/container:flat_hash_map",
        "@com_google_absl//absl/container:node_hash_map",
        "@com_google_absl//absl/container:btree",
        "@com_google_absl//absl/container:fixed_array",
    ],
)

