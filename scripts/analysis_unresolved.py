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

"""Script to analyze unresolved types from a YAML file."""
import argparse

import yaml


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
        yaml_data = yaml.safe_load(f)
        return yaml_data
      except yaml.YAMLError as e:
        print(f'Error parsing YAML in {filename}: {e}')
        return None
  except FileNotFoundError:
    print(f"Error: File '{filename}' not found.")
    return None
  except Exception as e:  # pylint: disable=broad-exception-caught
    print(f'An unexpected error occurred: {e}')
    return None


def entry_equals(e1, e2):
  if len(e1) != len(e2):
    return False
  else:
    for i, e in enumerate(e1):
      if e1[i]['function_name'] != e2[i]['function_name']:
        return False
      if e1[i]['line_offset'] != e2[i]['line_offset']:
        return False
      if e1[i]['column'] != e2[i]['column']:
        return False
  return True


if __name__ == '__main__':

  parser = argparse.ArgumentParser(description='Read and parse a YAML file.')
  parser.add_argument('filename', help='The path to the YAML file.')

  args = parser.parse_args()

  data = read_yaml_file(args.filename)

  s = list()
  # Assume structure is as  follows:
  # - Entry:
  # - function_name: xxx
  #   line_offset: 1
  #   column: 1
  if data is not None:
    for i, entry in enumerate(data):
      current_entry = entry['entry']
      equals = False
      for e2 in s:
        c = entry_equals(current_entry, e2['entry'])
        if c:
          equals = True
      if not equals:
        s.append(entry)

  print(s)
  print(len(s))

  with open('data.yml', 'w') as outfile:
    yaml.dump(s, outfile, default_flow_style=False)
