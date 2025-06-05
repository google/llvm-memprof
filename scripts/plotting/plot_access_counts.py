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

"""Plotting script for field access counts."""

import argparse
import glob

import yaml


def add_access_count(access_counts, profile):
  """Adds the field access count of a profile to a dictionary.

  Args:
    access_counts: A dictionary mapping type names to field access counts.
    profile: A dictionary containing the profile information.
  """

  type_name = profile['TypeName']

  if type_name == '_Rb_tree_node_base':
    print(profile, end='\n\n')

  if type_name in access_counts:
    access_counts[type_name] += profile['FieldAccessCount']
  else:
    access_counts[type_name] = profile['FieldAccessCount']

  try:
    for p_internal in profile['Children']:
      add_access_count(access_counts, p_internal)
  except KeyError:
    return
  except TypeError as e:
    print(e)
    exit(0)


parser = argparse.ArgumentParser(description='Plot Yaml Results')
parser.add_argument('input', type=str, help='build dir with yaml files to plot')


args = parser.parse_args()

yaml_full = []

for file in glob.glob(args.input + 'default.memprof.*.yaml'):

  with open(file) as stream:
    try:
      Out = yaml.safe_load(stream)
      if Out is not None:
        yaml_full = Out + yaml_full

    except yaml.YAMLError as exc:
      print(exc)


SUM_ACCESS = 0

access_counts = {}


for p in yaml_full:
  add_access_count(access_counts, p)
  SUM_ACCESS += p['FieldAccessCount']

print('Total Acess of bench run: ', SUM_ACCESS)

print(access_counts)

sum_access_histogram = 0


for p in access_counts:
  sum_access_histogram += access_counts[p]

access_counts_sorted = {
    k: v
    for k, v in sorted(access_counts.items(), key=lambda item: item[1])
}


for p in access_counts_sorted:
  print(p, end=': ')
  print(access_counts_sorted[p])

print(
    'Total Acess of summing all individual type acesses: ', sum_access_histogram
)

with open(args.input + 'fieldaccesscount.yml', 'w') as outfile:
  yaml.dump(yaml_full, outfile, default_flow_style=False)
