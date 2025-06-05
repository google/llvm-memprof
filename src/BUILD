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

load("@buildifier_prebuilt//:rules.bzl", "buildifier")
load("@rules_cc//cc:defs.bzl", "cc_binary", "cc_library", "cc_test", "cc_proto_library")
load("@rules_proto//proto:defs.bzl", "proto_library")

package(default_visibility = ["//visibility:public"])

buildifier(
    name = "buildifier",
    lint_mode = "warn",
    mode = "check",
    verbose = True,
)

cc_binary(
    name = "field_access_tool",
    srcs = [
        "field_access_tool.cc",
    ],
    deps = [
        ":histogram_builder",
        "@com_google_absl//absl/flags:flag",
        "@com_google_absl//absl/flags:parse",
    ],
)

cc_test(
    name = "type_resolver_test",
    srcs = ["type_resolver_test.cc"],
    data = [":testdata"],
    deps = [
        ":binary_file_retriever",
        ":dwarf_metadata_fetcher",
        ":test_status_macros",
        ":type_resolver",
        ":type_tree",
        ":object_layout_cc_proto",
        "@bazel_tools//src/main/cpp/util",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/strings:string_view",
        "@com_google_googletest//:gtest_main",
        "@com_google_googletest//:gtest",
    ],
)

cc_library(
    name = "type_resolver",
    srcs = ["type_resolver.cc"],
    hdrs = ["type_resolver.h"],
    deps = [
        ":dwarf_metadata_fetcher",
        ":type_tree",
        ":type_tree_container_blueprints",
        ":object_layout_cc_proto",
        "@com_google_absl//absl/container:flat_hash_set",
        "@com_google_absl//absl/log",
        "@com_google_absl//absl/log:check",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/strings:string_view",
        "@com_google_absl//absl/types:span",
        "@com_googlesource_code_re2//:re2",
        "@llvm-project//llvm:Demangle",
        "@status_macros//:status_macros",
    ],
)

cc_library(
    name = "type_tree",
    srcs = ["type_tree.cc"],
    hdrs = ["type_tree.h"],
    deps = [
        ":dwarf_metadata_fetcher",
        ":object_layout_cc_proto",
        "@com_google_absl//absl/container:flat_hash_map",
        "@com_google_absl//absl/log",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/strings:string_view",
        "@status_macros//:status_macros",
    ],
)

cc_library(
    name = "type_tree_container_blueprints",
    hdrs = ["type_tree_container_blueprints.h"],
    deps = [
        ":object_layout_cc_proto",
        "@com_google_absl//absl/log",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/strings:string_view",
    ],
)

cc_test(
    name = "histogram_builder_test",
    srcs = ["histogram_builder_test.cc"],
    data = [":testdata"],
    deps = [
        ":binary_file_retriever",
        ":dwarf_metadata_fetcher",
        ":histogram_builder",
        ":object_layout_cc_proto",
        ":test_status_macros",
        ":type_resolver",
        ":type_tree",
        "@bazel_tools//src/main/cpp/util",
        "@com_google_googletest//:gtest_main",
        "@com_google_googletest//:gtest",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/strings:string_view",
        "@com_google_absl//absl/types:span",
        "@llvm-project//llvm:ProfileData",
    ],
)

cc_library(
    name = "histogram_builder",
    srcs = ["histogram_builder.cc"],
    hdrs = ["histogram_builder.h"],
    deps = [
        ":dwarf_metadata_fetcher",
        ":type_resolver",
        ":type_tree",
        ":object_layout_cc_proto",
        "@com_google_absl//absl/container:flat_hash_map",
        "@com_google_absl//absl/log",
        "@com_google_absl//absl/log:check",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/strings:str_format",
        "@com_google_absl//absl/strings:string_view",
        "@com_google_absl//absl/time",
        "@com_google_absl//absl/types:span",
        "@llvm-project//llvm:Demangle",
        "@llvm-project//llvm:Object",
        "@llvm-project//llvm:ProfileData",
        "@llvm-project//llvm:Support",
        "@status_macros//:status_macros",
    ],
)

cc_test(
    name = "dwarf_metadata_fetcher_test",
    size = "small",
    srcs = [
        "dwarf_metadata_fetcher_test.cc",
    ],
    data = [
        ":testdata",
    ],
    deps = [
        ":binary_file_retriever",
        ":dwarf_metadata_fetcher",
        ":test_status_macros",
        "@bazel_tools//src/main/cpp/util",
        "@com_google_googletest//:gtest_main",
        "@com_google_googletest//:gtest",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/strings:string_view",
        "@status_macros//:status_macros",
    ],
)

cc_library(
    name = "binary_file_retriever",
    srcs = ["binary_file_retriever.cc"],
    hdrs = ["binary_file_retriever.h"],
    deps = [
        "@com_google_absl//absl/container:flat_hash_map",
        "@com_google_absl//absl/log",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
    ]
)

cc_library(
    name = "dwarf_metadata_fetcher",
    srcs = ["dwarf_metadata_fetcher.cc"],
    hdrs = ["dwarf_metadata_fetcher.h"],
    deps = [
        ":binary_file_retriever",
        "@com_google_absl//absl/base:core_headers",
        "@com_google_absl//absl/container:flat_hash_map",
        "@com_google_absl//absl/container:flat_hash_set",
        "@com_google_absl//absl/log",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/synchronization",
        "@com_google_absl//absl/time",
        "@llvm-project//llvm:BinaryFormat",
        "@llvm-project//llvm:DebugInfo",
        "@llvm-project//llvm:DebugInfoDWARF",
        "@llvm-project//llvm:Object",
        "@llvm-project//llvm:Support",
        "@status_macros//:status_macros",
    ],
)

cc_library(
    name = "test_status_macros",
    hdrs = ["test_status_macros.h"],
    deps = [
        "@com_google_absl//absl/status",
    ]
)

proto_library(
    name = "object_layout_proto",
    srcs = ["object_layout.proto"],
)

cc_proto_library(
    name = "object_layout_cc_proto",
    deps = [":object_layout_proto"],
)