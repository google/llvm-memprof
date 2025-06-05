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

package(default_visibility = ["//visibility:public"])

filegroup(name = "empty")

cc_toolchain_suite(
    name = "clang_suite",
    toolchains = {
        "k8": ":memprof_toolchain",
    },
)

cc_toolchain(
    name = "memprof_toolchain",
    toolchain_identifier = "memprof-toolchain",
    toolchain_config = ":memprof_toolchain_config",
    all_files = ":empty",
    compiler_files = ":empty",
    dwp_files = ":empty",
    linker_files = ":empty",
    objcopy_files = ":empty",
    strip_files = ":empty",
    supports_param_files = 0,
)


load(":cc_toolchain_config.bzl", "cc_toolchain_config")
cc_toolchain_config(name = "memprof_toolchain_config")
