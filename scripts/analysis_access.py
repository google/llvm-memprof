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

"""Script to analyze type access patterns from YAML files."""

import argparse
import math
import re

import matplotlib.pyplot as plt
import numpy as np
import yaml


def simple_template(input_string):
  match = re.search(r'^(.*?)(?:<|$)', input_string)
  if match:
    first_part = match.group(1)
    cleaned_part = re.sub(r'[a-zA-Z0-9_:]+::', '', first_part).strip()
    return cleaned_part
  return re.sub(r'[a-zA-Z0-9_:]+::', '', input_string).strip()


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
  offset_ = 0
  access_ = 0
  children_ = list()

  def is_leaf(self):
    return not self.children_

  def __init__(self, node):
    self.full_type_name_ = node['type']
    self.type_name_ = simple_template(node['type'])
    if 'name' in node:
      try:
        self.name_ = simple_template(node['name'])
      except KeyError:
        self.name = node['name']
    else:
      self.name_ = 'padding'
    self.size_ = node['size']
    self.offset_ = node['global_offset']
    self.access_ = node['total_access']
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
    print(prefix + self.type_name_, end=' ')
    print(self.name_, end=' ')
    print(self.size_, end=' ')
    print(self.offset_, end=' ')
    print(self.access_)
    if self.children_:
      for c in self.children_:
        c.print(level + 1)


class TypeTree:
  """Represents a tree structure for type information."""
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


def read_yaml_file(filename):
  """Reads a YAML file and returns the parsed data.

  Args:
    filename: The path to the YAML file.

  Returns:
    A dictionary containing the parsed YAML data, or None if an error occurred.
  """
  try:
    with open(filename, 'r') as f:
      try:
        data = yaml.safe_load(f)
        return data
      except yaml.YAMLError as e:
        print(f'Error parsing YAML in {filename}: {e}')
        return None
  except FileNotFoundError:
    print(f"Error: File '{filename}' not found.")
    return None
  except Exception as e:  # pylint: disable=broad-exception-caught
    print(f'An unexpected error occurred: {e}')
    return None


def create_bar_chart(
    data, categories, title, xlabel, ylabel, stacked=False, color=None, ax=None
):
  """Creates a bar chart.

  Args:
    data: The data to plot.
    categories: The categories for the x-axis.
    title: The title of the chart.
    xlabel: The label for the x-axis.
    ylabel: The label for the y-axis.
    stacked: Whether to stack the bars.
    color: The color of the bars.
    ax: The axes to plot on.

  Returns:
    The axes.
  """

  num_series = len(data)
  num_categories = len(data[0])

  x = np.arange(num_categories)
  width = 0.15

  if ax is None:
    _, ax = plt.subplots(figsize=(100, 60))

  for series_idx in range(num_series):
    offset = series_idx * width
    ax.bar(x + offset, data[series_idx], width, label=title, color=color)

  ax.set_yscale('log')  # Set y-axis to logarithmic scale
  ax.set_xticks(x + (num_series - 1) * width / 2)

  ax.set_yticks([])
  ax.set_xticks([])
  plt.tight_layout()  # Important for subplots

  return ax  # Return the axes


if __name__ == '__main__':

  parser = argparse.ArgumentParser(description='Read and parse a YAML file.')
  parser.add_argument('filename', help='The path to the YAML file.')

  args = parser.parse_args()

  yaml_data = read_yaml_file(args.filename)

  s = list()
  # Assume structure is as  follows:
  # - Entry:
  # - TypeTree:
  # ----
  # - Callstack:
  # - function_name: xxx
  #   line_offset: 1
  #   column: 1
  if yaml_data is not None:
    num_plots = len(yaml_data)
    num_cols_plots = 4
    fig, axes = plt.subplots(
        math.ceil(num_plots / num_cols_plots),
        num_cols_plots,
        figsize=(150, 100),
    )

    axes = axes.flatten()

    for ax in axes:
      ax.set_yticks([])
      ax.set_xticks([])

    callstacks = list()
    type_trees = list()

    for i, entry in enumerate(yaml_data):
      callstack_yaml = entry['Entry']['callstack']
      callstack = Callstack(callstack_yaml)
      callstacks.append(callstack)
      tree_yaml = entry['Entry']['type_tree']
      tree = TypeTree(tree_yaml)
      type_trees.append(tree)

    for i, (callstack, tree) in enumerate(zip(callstacks, type_trees)):
      flat = tree.flatten()
      if tree.root_:
        print(tree.root_.full_type_name_)
      flat.print()
      print('--------------------------------------\n')
      values = [x.access_ for x in flat.nodes_]
      labels = [x.name_ + ' : ' + x.type_name_ for x in flat.nodes_]
      labels = ['' for _ in flat.nodes_]
      create_bar_chart(
          [values],
          labels,
          '',
          'Categories',
          'Values',
          stacked=False,
          ax=axes[i],
      )

    plt.show()
    # exit(0)
