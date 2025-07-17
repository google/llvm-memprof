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

workspace(name = "com_google_llvm-memprof")

load("@bazel_tools//tools/build_defs/repo:http.bzl", "http_archive")

http_archive(
    name = "bazel_skylib",
    sha256 = "bc283cdfcd526a52c3201279cda4bc298652efa898b10b4db0837dc51652756f",
    urls = [
        "https://mirror.bazel.build/github.com/bazelbuild/bazel-skylib/releases/download/1.7.1/bazel-skylib-1.7.1.tar.gz",
        "https://github.com/bazelbuild/bazel-skylib/releases/download/1.7.1/bazel-skylib-1.7.1.tar.gz",
    ],
)

new_local_repository(
    name = "com_google_absl",
    build_file_content = "# empty",
    path = "third_party/abseil-cpp",

)

new_local_repository(
    name = "com_google_fleetbench",
    build_file_content = "# empty",
    path = "third_party/fleetbench",

)

load("@bazel_skylib//:workspace.bzl", "bazel_skylib_workspace")


bazel_skylib_workspace()

http_archive(
    name = "com_google_googletest",
    sha256 = "ce7366fe57eb49928311189cb0e40e0a8bf3d3682fca89af30d884c25e983786",
    strip_prefix = "googletest-release-1.12.0",
    urls = ["https://github.com/google/googletest/archive/refs/tags/release-1.12.0.zip"],
)


http_archive(
    name = "com_googlesource_code_re2",
    sha256 = "0a890c2aa0bb05b2ce906a15efb520d0f5ad4c7d37b8db959c43772802991887",
    strip_prefix = "re2-a427f10b9fb4622dd6d8643032600aa1b50fbd12",
    urls = ["https://github.com/google/re2/archive/a427f10b9fb4622dd6d8643032600aa1b50fbd12.zip"],  # 2022-06-09
)


http_archive(
    name = "buildifier_prebuilt",
    sha256 = "72b5bb0853aac597cce6482ee6c62513318e7f2c0050bc7c319d75d03d8a3875",
    strip_prefix = "buildifier-prebuilt-6.3.3",
    urls = [
        "http://github.com/keith/buildifier-prebuilt/archive/6.3.3.tar.gz",
    ],
)


http_archive(
    name = "com_google_protobuf",
    sha256 = "da288bf1daa6c04d03a9051781caa52aceb9163586bff9aa6cfb12f69b9395aa",
    strip_prefix = "protobuf-27.0",
    url = "https://github.com/protocolbuffers/protobuf/releases/download/v27.0/protobuf-27.0.tar.gz",
)
load("@com_google_protobuf//:protobuf_deps.bzl", "protobuf_deps")
protobuf_deps()

http_archive(
    name = "rules_cc",
    urls = ["https://github.com/bazelbuild/rules_cc/releases/download/0.1.1/rules_cc-0.1.1.tar.gz"],
    sha256 = "712d77868b3152dd618c4d64faaddefcc5965f90f5de6e6dd1d5ddcd0be82d42",
    strip_prefix = "rules_cc-0.1.1",
)

new_local_repository(
    name = "llvm-raw",
    build_file_content = "# empty",
    path = "third_party/llvm-project",
)

load("@llvm-raw//utils/bazel:configure.bzl", "llvm_configure")

llvm_configure(name = "llvm-project")

load("@bazel_tools//tools/build_defs/repo:utils.bzl", "maybe")

maybe(
    http_archive,
    name = "llvm_zlib",
    build_file = "@llvm-raw//utils/bazel/third_party_build:zlib-ng.BUILD",
    sha256 = "e36bb346c00472a1f9ff2a0a4643e590a254be6379da7cddd9daeb9a7f296731",
    strip_prefix = "zlib-ng-2.0.7",
    urls = [
        "https://github.com/zlib-ng/zlib-ng/archive/refs/tags/2.0.7.zip",
    ],
)

maybe(
    http_archive,
    name = "llvm_zstd",
    build_file = "@llvm-raw//utils/bazel/third_party_build:zstd.BUILD",
    sha256 = "7c42d56fac126929a6a85dbc73ff1db2411d04f104fae9bdea51305663a83fd0",
    strip_prefix = "zstd-1.5.2",
    urls = [
        "https://github.com/facebook/zstd/releases/download/v1.5.2/zstd-1.5.2.tar.gz",
    ],
)

http_archive(
    name = "rules_python",
    sha256 = "9f9f3b300a9264e4c77999312ce663be5dee9a56e361a1f6fe7ec60e1beef9a3",
    strip_prefix = "rules_python-1.4.1",
    url = "https://github.com/bazel-contrib/rules_python/releases/download/1.4.1/rules_python-1.4.1.tar.gz",
)

load("@rules_python//python:repositories.bzl", "py_repositories")

py_repositories()

http_archive(
    name = "rules_pkg",
    urls = [
        "https://mirror.bazel.build/github.com/bazelbuild/rules_pkg/releases/download/1.1.0/rules_pkg-1.1.0.tar.gz",
        "https://github.com/bazelbuild/rules_pkg/releases/download/1.1.0/rules_pkg-1.1.0.tar.gz",
    ],
    sha256 = "b7215c636f22c1849f1c3142c72f4b954bb12bb8dcf3cbe229ae6e69cc6479db",
)
load("@rules_pkg//:deps.bzl", "rules_pkg_dependencies")
rules_pkg_dependencies()

STATUS_MACROS_COMMIT = "c2fcdb9b0d29ae60470c98c0f6f8ea42daad942c"
http_archive(
    name = "status_macros",
    strip_prefix = "status_macros-" + STATUS_MACROS_COMMIT,
    url = "https://github.com/jimrogerz/status_macros/archive/%s.zip" % STATUS_MACROS_COMMIT,
)


http_archive(
    name = "com_google_tcmalloc",  # 2025-01-07
    integrity = "sha256-e3p59ZEQU8BDXQmrl+9MUxTSz5mPgWYUKX4/t02B7BI=",
    strip_prefix = "tcmalloc-b6563dbdc7905f7b0b31c97256c83e7c9b2f08c2",
    urls = ["https://github.com/google/tcmalloc/archive/b6563dbdc7905f7b0b31c97256c83e7c9b2f08c2.zip"],
)
