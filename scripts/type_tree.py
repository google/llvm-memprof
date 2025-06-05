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

"""Helper functions and class definitions for Type tree."""

import math
import re
import sys

import yaml


def format_large_number(number):
  """Formats a large number with appropriate suffixes (K, M, B, T)."""
  if number < 1000:
    return str(number)

  suffixes = ['', 'K', 'M', 'B', 'T']
  magnitude = 0
  while abs(number) >= 1000:
    magnitude += 1
    number /= 1000

  if magnitude >= len(suffixes):
    return f'{number:.2f}E+{magnitude*3}'

  return f'{number:.1f}{suffixes[magnitude]}'


def format_percentage(number, decimals=0):
  percentage = number * 100
  return f'{percentage:.{decimals}f}%'


def remove_array_size(text):
  return re.sub(r'\[\d+\]', '[]', text)


def simple_template(input_string):
  match = re.search(r'^(.*?)(?:<|$)', input_string)
  if match:
    first_part = match.group(1)
    cleaned_part = re.sub(r'[a-zA-Z0-9_:]+::', '', first_part).strip()
    return cleaned_part
  return re.sub(r'[a-zA-Z0-9_:]+::', '', input_string).strip()


def create_range_list(min_value, max_value, num_entries):
  step = (max_value - min_value) / (num_entries - 1) if num_entries > 1 else 0
  result_list = []
  for i in range(num_entries):
    value = min_value + i * step
    result_list.append(value)

  return result_list


def create_log_range_list(min_value, max_value, num_entries):
  log_min = math.log(min_value)
  log_max = math.log(max_value)
  log_step = (log_max - log_min) / (num_entries - 1)

  result_list = []
  for i in range(num_entries):
    log_value = log_min + i * log_step
    value = math.exp(log_value)  # Convert back from log space
    result_list.append(value)

  return result_list


def hotness_buckets(type_trees):
  min_access = sys.maxsize
  max_access = -1
  for t in type_trees:
    if t.root_.access_ < min_access:
      min_access = t.root_.access_
    if t.root_.access_ > max_access:
      max_access = t.root_.access_
  if min_access == 0:
    min_access = 1
  return create_log_range_list(min_access, max_access, 8)


class Callsite:
  """Represents a callsite in the callstack."""

  function_name_ = ''
  line_offset_ = 0
  col_ = 0

  def __init__(self, callstite_yaml):
    self.function_name = callstite_yaml['function_name']
    self.line_offset_ = callstite_yaml['line_offset']
    self.col_ = callstite_yaml['column']

  def print(self):
    print(self.function_name, end=' ')
    print(self.line_offset_, end=' ')
    print(self.col_)


class Callstack:
  callsites = list()

  def __init__(self, entries):
    for e in entries:
      self.callsites.append(Callsite(e))

  def print(self):
    for c in self.callsites:
      c.print()


class FlatTree:
  nodes_ = list()

  def append(self, node):
    self.nodes_.append(node)

  def print(self):
    for n in self.nodes_:
      n.print(0)


class Node:
  """Represents a node in the type tree."""

  full_type_name_ = ''
  type_name_ = ''
  name_ = ''
  size_ = 0
  multiplicity_ = 1
  offset_ = 0
  access_ = 0
  hotness_ = 1
  problem = False
  children_ = list()

  def is_leaf(self):
    return not self.children_

  def __init__(self, node):
    self.full_type_name_ = node['type']
    self.type_name_ = simple_template(node['type'])
    if 'name' in node:
      try:
        self.name_ = simple_template(node['name'])
      except Exception:  # pylint: disable=broad-except
        self.name = node['name']
    else:
      self.name_ = 'padding'
    self.size_ = node['size']
    if self.size_ < 0:
      self.size_ = 10
      self.problem = True
    self.offset_ = node['global_offset']
    self.access_ = node['total_access']
    if 'multiplicity' in node:
      self.multiplicity_ = node['multiplicity']
    if 'children' not in node:
      return
    self.add_children(node['children'])

  def add_children(self, children_yaml):
    children_list = list()
    for child in children_yaml:
      children_list.append(Node(child))
    self.children_ = children_list

  def flatten(self, result):
    if self.is_leaf():
      result.append(self)
    else:
      for c in self.children_:
        c.flatten(result)

  def print(self, level):
    prefix = level * '  '
    print(prefix + self.full_type_name_, end=' ')
    print(self.name_, end=' ')
    print(self.size_, end=' ')
    print(self.access_)
    if self.children_:
      for c in self.children_:
        c.print(level + 1)

  def dearray(self):
    self.name_ = remove_array_size(self.name_)
    self.type_name_ = remove_array_size(self.type_name_)
    self.full_type_name_ = remove_array_size(self.full_type_name_)
    if self.is_leaf():
      return
    new_size = 0
    for c in self.children_:
      c.dearray()
      new_size += c.size_
    self.size_ = new_size

  def hotness(self, h_buckets):
    """Calculates the hotness of a node based on access frequency."""
    b = False
    index = 0
    for index, max_cut in enumerate(h_buckets):
      if self.access_ < max_cut:
        self.hotness_ = index + 1
        b = True
        break
    if not b:
      self.hotness_ = index + 2
    if self.problem:
      self.hotness_ = 11  # this special color for problems
    for c in self.children_:
      c.hotness(h_buckets)

  def flamegraph(self, prefix, total_access):
    next_prefix = f'{prefix}{self.flamegraph_string(total_access)};'
    if self.is_leaf():
      print(f'{prefix}{self.flamegraph_string(total_access)} {self.size_}')
    else:
      print(f'{prefix}{self.flamegraph_string(total_access)} 0')
    for c in self.children_:
      c.flamegraph(next_prefix, total_access)

  def flamegraph_string(self, total_access):
    return (
        f'({format_large_number(self.access_)},'
        f' {format_percentage(self.access_ / total_access)}, {self.size_}B)'
        f' {self.name_}:{self.full_type_name_}_[h{self.hotness_}]'
    )


class TypeTree:
  """Represents a type tree."""

  root_ = None

  def __init__(self, type_tree):
    self.root_ = Node(type_tree['tree'][0])

  def print(self):
    if self.root_:
      self.root_.print(0)

  def flatten(self):
    result = FlatTree()
    if self.root_:
      self.root_.flatten(result)
    return result

  def dearray(self):
    if self.root_:
      self.root_.dearray()

  def flamegraph(self, total_access):
    if self.root_:
      self.root_.flamegraph('', total_access)

  def hotness(self, h_buckets):
    if self.root_:
      self.root_.hotness(h_buckets)


def read_yaml_file(filename):
  """Reads a YAML file and returns the parsed data."""
  try:
    with open(filename, 'r') as f:
      try:
        yaml_data = yaml.safe_load(f)
        return yaml_data
      except yaml.YAMLError as e:
        print(f'Error parsing YAML in {filename}: {e}')
        return None
  except FileNotFoundError:
    print(f"Error: File '{filename}' not found.")
    return None
  except yaml.YAMLError as e:
    print(f'An unexpected error occurred: {e}')
    return None
