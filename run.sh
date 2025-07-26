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


#!/bin/bash

log() {
    echo "[INFO] $*"
}

function extract_workload_name() {
  local workload_string="$1"
  log "${workload_string##*.}"
}

function is_workload_selected() {
  local workload="$1"
  local -a selected_list
  local index=0

  for arg in "${@:2}"; do
    selected_list[index]="$arg"
    ((index++))
  done

  for item in "${selected_list[@]}"; do
    if [[ "$workload" == "$item" ]]; then
      return 0
    fi 
  done
  return 1
}

function process_workload_memory_profiles() {
  local profile_dir="$1"
  local workload="$2"
  local binary_path="$3"

  find "$profile_dir" -maxdepth 1 -type f -name "memprof.profraw.*" -print0 | while IFS= read -r -d $'\0' profile_file; do
    local full_command="$base_command \"$binary_path\" --memprof_profile \"$profile_file\" --verify_verbose"

    local typetree="$OUT/${workload}.typetree"
    log "$full_command --verify_verbose 2> $OUT/${workload}.verbose 1>  $typetree"
    eval "$full_command --verify_verbose 2> $OUT/${workload}.verbose 1>  $typetree"
    eval "cat $OUT/${workload}.verbose | grep '====== Statistics ======' -A 14 > $OUT/${workload}.stats"

    log "llvm-profdata show $profile_file \
    --profiled-binary=$binary_path --memory > $OUT/${workload}.profdata"
    eval "llvm-profdata show $profile_file \
    --profiled-binary=$binary_path --memory > $OUT/${workload}.profdata"

    log "$full_command --dump_unresolved_callstacks  1>  $OUT/${workload}.unresolved 2> /dev/null"
    eval "$full_command --dump_unresolved_callstacks  1>  $OUT/${workload}.unresolved 2> /dev/null"

    log "python3 ${SCRIPTS_DIR}/flamegraph.py $typetree | $FLAMEGRAPH_DIR/flamegraph.pl > $OUT/${workload}.svg"
    eval "python3 ${SCRIPTS_DIR}/flamegraph.py --max 10 $typetree | $FLAMEGRAPH_DIR/flamegraph.pl > $OUT/${workload}_10.svg"
    eval "python3 ${SCRIPTS_DIR}/flamegraph.py --max 50 $typetree | $FLAMEGRAPH_DIR/flamegraph.pl > $OUT/${workload}_50.svg"
    eval "python3 ${SCRIPTS_DIR}/flamegraph.py --max 200 $typetree | $FLAMEGRAPH_DIR/flamegraph.pl > $OUT/${workload}_200.svg"

    log "  -----------------------------------------"
  done

  log "Finished processing workload: $workload"
  cat $OUT/${workload}.stats
  log "========================================="
}



 # ============SPEC====================
spec_workloads=(
  "508.namd_r"
  "510.parest_r"
  "520.omnetpp_r"
  "523.xalancbmk_r"
  "541.leela_r"
)
build_spec(){
  log "Building SPEC..."

  # Get rid of nusance code that prevents building with clang. Clang has no flag that will accept this error...
  find "${SPEC_DIR}" \( -name '*.cc' -o -name '*.cpp' -o -name '*.h' -o -name '*.hpp' \) \
    -exec sed -i "s|(s\.c_str() != '\\\\0') || (\*endptr == '\\\\0')|(!s.empty() || (*endptr == '\\0'))|g" {} +

  # Copy cfg into spec directory
  cp "${TOP_DIR}/cfg/memprof.cfg" "${SPEC_DIR}/config/memprof.cfg"

  # Replace LLVM_PATH in memprof.cfg with absolute path of this repo
  sed -i "s|^[[:space:]]*LLVM_PATH[[:space:]]*=.*|LLVM_PATH = ${TOP_DIR}/third_party/llvm-project/|" "${SPEC_DIR}/config/memprof.cfg"

  cd $SPEC_DIR
  source shrc
  for workload in "${spec_workloads[@]}"; do
    log "Scrub workload: $workload"
    build_cmd="runcpu --config=memprof --action=scrub ${workload}"
    log ${build_cmd}
    ${build_cmd}
    log "Building workload: $workload"
    build_cmd="runcpu --config=memprof --action=build ${workload}"
    log ${build_cmd}
    ${build_cmd}
    log "Running and collecting memory profiles for: $workload"
    build_cmd="runcpu --config=memprof --action=run --size=test ${workload}"
    log ${build_cmd}
    ${build_cmd}
  done
}
run_spec() {
  log "Processing SPEC workloads..."
  for workload in "${spec_workloads[@]}"; do
    is_workload_selected "${workload}" "$@"
    local is_selected=$?
    if [[ "${is_selected}" -ne 0 ]]; then
      continue
    fi
    log "Processing workload: $workload"
    workload_name=$(extract_workload_name "$workload")

    if [[ "${workload_name}" == xalancbmk* ]]; then
      workload_name=("cpuxalan_r")
    fi

    profile_dir="$SPEC_DIR/$workload/build/build_peak_mytest-m64.0000/default_%p.memprof.profraw/"
    binary_path="$SPEC_DIR/$workload/build/build_peak_mytest-m64.0000/${workload_name}_memprof"
    
    process_workload_memory_profiles "$profile_dir" "$workload_name" "$binary_path"
  done
  log "Finished processing all specified SPEC workloads."
}



# ============CLANG====================
clang_workloads=(
  "llvm-dwarfdump"
  "llvm-objdump"
  "clang"
)

run_clang() {
  local selected_benchmarks="$1"
  log "Processing CLANG workloads..."
  for workload in "${clang_workloads[@]}" ; do
    is_workload_selected "${workload}" "$@"
    local is_selected=$?
    if [[ "${is_selected}" -ne 0 ]]; then
      continue
    fi
    log "Processing CLANG workload: $workload"
    dir="${TOP_DIR}/integration_tests/clang/tests/${workload}"
    binary="${TOP_DIR}/${workload}"
    process_workload_memory_profiles "$dir"  "$workload" "$binary"
  done
  log "Finished processing CLANG workloads."
}

# ============Fleetbench====================
fleetbench_workloads=(
  "proto_benchmark"
  "swissmap_benchmark"
  # "mem_benchmark" --> memprof format invalid
  "empirical_driver"
  "compression_benchmark"
  "hashing_benchmark"
  "cord_benchmark"
  "rpc_benchmark"
)

fleet_bench_build_target=(
  "proto"
  "swissmap"
  # "libc" --> memprof format invalid
  "tcmalloc"
  "compression"
  "hashing"
  "stl"
  "rpc"
)

function run_fleetbench() {
  log "Processing Fleetbench workloads..."
  for i in "${!fleetbench_workloads[@]}"; do

    workload="${fleetbench_workloads[$i]}"
    build_target="${fleet_bench_build_target[$i]}"
    is_workload_selected "${workload}" "$@"
    local is_selected=$?
    if [[ "${is_selected}" -ne 0 ]]; then
      continue
    fi
    # Step 1: Build and Run Fleetbench workload with Memprof
    rm /tmp/memprof.profraw.*
    BUILD_FLEETBENCH_CMD="bazel run --fission=no --strip=never  \
    -c dbg --fdo_instrument=/tmp \
    --copt=-g --copt=-fdebug-info-for-profiling --copt=-O0 \
    --copt=-mllvm --copt=-memprof-histogram --copt=-fdebug-info-for-profiling \
    --copt=-mno-omit-leaf-frame-pointer --copt=-fno-omit-frame-pointer --features=-simple_template_names \
    --copt=-fno-optimize-sibling-calls --copt=-m64 --copt=-Wl,--copt=-build-id \
    --copt=-no-pie third_party/fleetbench/${build_target}:${workload}"
    log "Building Fleetbench workload: ${workload}:"
    log "$BUILD_FLEETBENCH_CMD"
    eval "$BUILD_FLEETBENCH_CMD"
    rm -rf /tmp/${workload}
    cp bazel-bin/third_party/fleetbench/${build_target}/${workload} /tmp/${workload}
    
    # Step 2: Process the workload memory profiles
    process_workload_memory_profiles "/tmp" "${workload}" "/tmp/${workload}"
  done
  log "Finished processing Fleetbench workloads."
}


folly_workloads=()

function run_folly() {
  log "Processing Folly workloads..."
  # ============Folly====================
  if [[ -n "${folly_workloads[@]}" ]]; then
    log "No Folly workloads defined."
  fi
  log "Finished processing Folly workloads."
}




function show_help() {
  log "Usage: $0 [OPTIONS]"
  log "Options:"
  log "  --all         Run all workload categories (spec, clang, fleetbench, folly)."
  log "  --build-spec  Build SPEC workloads."
  log "  --spec        Run SPEC workloads."
  log "  --clang       Run CLANG workloads."
  log "  --fleetbench  Run Fleetbench workloads."
  log "  --folly       Run Folly workloads."
  log "  --benchmarks <benchmark1,benchmark2,...>"
  log "                Run only the specified benchmarks (comma-separated). Only runs if --<category> is specified also."
  log "                Available benchmarks:"
  if [[ ${#spec_workloads[@]} -gt 0 ]]; then
    log "                  SPEC: ${spec_workloads[*]}"
  fi
  if [[ ${#clang_workloads[@]} -gt 0 ]]; then
    log "                  CLANG: ${clang_workloads[*]}"
  fi
  if [[ ${#fleetbench_workloads[@]} -gt 0 ]]; then
    log "                  Fleetbench: ${fleetbench_workloads[*]}"
  fi
  if [[ ${#folly_workloads[@]} -gt 0 ]]; then
    log "                  Folly: ${folly_workloads[*]}"
  fi
  log "  --help, -h    Show this help message."
  exit 1
}




#======MAIN====================================================================: 
[[ -n "${TOP_DIR:-}" ]] || { log "ERROR: TOP_DIR empty. Are you sure you sourced the environment? (source env.sh)" >&2; exit 1; }

current_datetime=$(date "+%Y-%m-%d_%H-%M-%S")
curr_experiment="${current_datetime}"
OUT="${TOP_DIR}/out/$curr_experiment"

# Define the base command (without the specific workload path or memprof file)
base_command="bazel run //src:field_access_tool -- --stats --local --memprof_profiled_binary"
 
mkdir -p "$OUT"
log "$OUT"

# Process command-line arguments
run_all=false
build_spec_flag=false
run_spec_flag=false
run_clang_flag=false
run_fleetbench_flag=false
run_folly_flag=false
benchmark_provided=false
selected_benchmarks=()
available_benchmarks=("${spec_workloads[@]}" "${clang_workloads[@]}" "${fleetbench_workloads[@]}" "${folly_workloads[@]}")


while [[ $# -gt 0 ]]; do
  case "$1" in
    --all)
      run_all=true
      ;;
    --build-spec)
      build_spec_flag=true
      ;;
    --spec)
      run_spec_flag=true
      ;;
    --clang)
      run_clang_flag=true
      ;;
    --fleetbench)
      run_fleetbench_flag=true
      ;;
    --folly)
      run_folly_flag=true
      ;;
    --benchmarks)
      if [[ -n "$2" ]]; then
        IFS=',' read -ra benchmarks <<< "$2"
        selected_benchmarks=("${benchmarks[@]}")
        benchmark_provided=true
        shift # Consume the value
      else
        log "Error: No benchmarks specified for --benchmark option '$2'"
        show_help
      fi
      shift # Consume the option
      ;;
    --help|-h)
      show_help
      ;;
    *)
      log "Error: Unknown option '$1'"
      show_help
      ;;
  esac
  shift
done

if $benchmark_provided; then
  log "Selecting benchmarks: ${selected_benchmarks[@]}"
  else
  log "No benchmarks selected. Using all available benchmarks."
  selected_benchmarks=("${available_benchmarks[@]}")
fi

log "Selected benchmarks: ${selected_benchmarks[@]}"

if [[ "$run_all" == true ]]; then
  log "Running all workload categories."
  run_spec "${selected_benchmarks[@]}"
  run_clang "${selected_benchmarks[@]}"
  run_fleetbench "${selected_benchmarks[@]}"
  run_folly
elif [[ "$run_spec_flag" == true ]]; then
  log "Running SPEC workloads."
  run_spec "${selected_benchmarks[@]}"
elif [[ "$build_spec_flag" == true ]]; then
  log "Building SPEC and getting memprof."
  build_spec
elif [[ "$run_clang_flag" == true ]]; then
  log "Running CLANG workloads."
  run_clang "${selected_benchmarks[@]}"
elif [[ "$run_fleetbench_flag" == true ]]; then
  log "Running Fleetbench workloads."
  run_fleetbench "${selected_benchmarks[@]}"
elif [[ "$run_folly_flag" == true ]]; then
  log "Running Folly workloads."
  run_folly "${selected_benchmarks[@]}"
else
  log "No workloads specified. Use --all or a specific category/benchmark."
  show_help
fi

exit 0


show_help
exit 0