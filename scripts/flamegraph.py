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

"""Script to generate flamegraph format from a YAML type tree file."""

import argparse

from type_tree import Callstack
from type_tree import hotness_buckets
from type_tree import read_yaml_file
from type_tree import TypeTree


if __name__ == "__main__":

  parser = argparse.ArgumentParser(description="Read and parse a YAML file.")
  parser.add_argument("filename", help="The path to the YAML file.")
  parser.add_argument(
      "--min", type=int, default=0, help="The path to the YAML file."
  )
  parser.add_argument(
      "--max", type=int, default=-1, help="The path to the YAML file."
  )

  args = parser.parse_args()

  yaml_data = read_yaml_file(args.filename)

  # Assume structure is as  follows:
  # - Entry:
  # - TypeTree:
  # ----
  # - Callstack:
  # - function_name: xxx
  #   line_offset: 1
  #   column: 1
  if yaml_data is not None:
    callstacks = list()
    type_trees = list()
    total_accesses = 0
    for i, entry in enumerate(yaml_data):
      callstack_yaml = entry["Entry"]["callstack"]
      callstack = Callstack(callstack_yaml)
      callstacks.append(callstack)

      tree_yaml = entry["Entry"]["type_tree"]
      tree = TypeTree(tree_yaml)
      tree.dearray()
      type_trees.append(tree)

    h_buckets = hotness_buckets(type_trees)

    zipped = zip(callstacks, type_trees)
    sorted_zipped = sorted(
        zipped, key=lambda x: x[1].root_.access_, reverse=True
    )
    if args.max != -1:
      sorted_zipped = sorted_zipped[0 : args.max]
    if args.max != -1 and args.min != 0:
      sorted_zipped = sorted_zipped[args.min : args.max]

    for _, tree in sorted_zipped:
      total_accesses += tree.root_.access_

    for i, (callstack, tree) in enumerate(sorted_zipped):
      tree.hotness(h_buckets)
      tree.flamegraph(total_accesses)
