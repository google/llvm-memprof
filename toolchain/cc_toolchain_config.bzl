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

"""
This module defines a custom Bazel C++ toolchain configuration for
using a locally built Clang compiler and LLVM tools.
"""
load("@bazel_tools//tools/build_defs/cc:action_names.bzl", "ACTION_NAMES")

load(
    "@bazel_tools//tools/cpp:cc_toolchain_config_lib.bzl",
    "feature",
    "flag_group",
    "flag_set",
    "tool_path",
)

all_link_actions = [
    ACTION_NAMES.cpp_link_executable,
    ACTION_NAMES.cpp_link_dynamic_library,
    ACTION_NAMES.cpp_link_nodeps_dynamic_library,
]

def _impl(ctx):
    LLVM_ROOT = ctx.var["LLVM_ROOT"]
    tool_paths = [
        tool_path(
            name = "gcc",
            path = LLVM_ROOT + "/install/bin/clang",
        ),
        tool_path(
            name = "clang",
            path =  LLVM_ROOT + "/install/bin/clang",
        ),
        tool_path(
            name = "clang++",
            path =  LLVM_ROOT + "/install/bin/clang++",
        ),
        tool_path(
            name = "cpp",
            path =  LLVM_ROOT + "/install/bin/clang++",
        ),
        tool_path(
            name = "ar",
            path =  LLVM_ROOT + "/install/bin/llvm-ar",
        ),
        tool_path(
            name = "ld",
            path =  LLVM_ROOT + "/install/bin/lld",
        ),
        tool_path(
            name = "gcov",
            path = "/bin/false",
        ),
        tool_path(
            name = "nm",
            path = "/bin/false",
        ),
        tool_path(
            name = "objdump",
            path = "/bin/false",
        ),
        tool_path(
            name = "strip",
            path = "/bin/false",
        ),
    ]

    features = [
        feature(
            name = "default_linker_flags",
            enabled = True,
            flag_sets = [
                flag_set(
                    actions = all_link_actions,
                    flag_groups = ([
                        flag_group(
                            flags = [
                                "-lstdc++",
                            ],
                        ),
                    ]),
                ),
            ],
        ),
        feature(
            name = "memprof",
            enabled = False,
            flag_sets = [
                flag_set(
                    actions = [
                        "c-compile",
                        "c++-compile",
                        "cc-compile",
                        "preprocess-assemble",
                    ],
                    flag_groups = [
                        flag_group(
                            flags = [
                                "-g",
                                "-fstandalone-debug",
                                "-fdebug-info-for-profiling",
                                "-mno-omit-leaf-frame-pointer",
                                "-fno-omit-frame-pointer",
                                "-fno-optimize-sibling-calls",
                                "-m64",
                                "-fmemory-profile=.",
                                "-fprofile-generate=.",
                                "-mllvm", "-memprof-histogram",
                                "-fno-exceptions",
                                "-fPIC",
                            ]
                        ),
                    ],
                ),
                flag_set(
                    actions = all_link_actions,
                    flag_groups = [
                        flag_group(
                            flags = [
                                "-fuse-ld=lld",
                                "-fPIC",
                                "-Wl,--no-rosegment",
                                "-mno-omit-leaf-frame-pointer",
                                "-Wl,-build-id",
                                "-no-pie",
                                "-fmemory-profile=.",
                                "-fprofile-generate=."
                            ]
                        )
                    ],
                )
            ],
        )
    ]

    return cc_common.create_cc_toolchain_config_info(
        ctx = ctx,
        cxx_builtin_include_directories = [
            LLVM_ROOT + "/install/llvm/include/",
            LLVM_ROOT + "/install/lib/clang/21/include/",
            LLVM_ROOT + "/install/include/c++/v1",
            "/usr/include",
        ],
        features = features,
        toolchain_identifier = "local",
        host_system_name = "local",
        target_system_name = "local",
        target_cpu = "k8",
        target_libc = "unknown",
        compiler = "clang",
        abi_version = "unknown",
        abi_libc_version = "unknown",
        tool_paths = tool_paths,
    )

cc_toolchain_config = rule(
    implementation = _impl,
    attrs = {},
    provides = [CcToolchainConfigInfo],
)
