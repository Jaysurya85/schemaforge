#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BINARY="${ROOT_DIR}/build/schemaforge"
RESULT_DIR="${ROOT_DIR}/benchmark-results"
ALL_CASES=(single_table_1m users_orders_1m ecommerce_large)

usage() {
  echo "Usage: scripts/run_benchmarks.sh [single_table_1m] [users_orders_1m] [ecommerce_large]"
}

is_known_case() {
  local requested="$1"
  local benchmark_case
  for benchmark_case in "${ALL_CASES[@]}"; do
    if [[ "${requested}" == "${benchmark_case}" ]]; then
      return 0
    fi
  done
  return 1
}

yaml_value() {
  local key="$1"
  local report="$2"
  awk -v key="${key}:" '$1 == key { print $2; exit }' "${report}"
}

format_seconds() {
  awk -v value="$1" 'BEGIN { printf "%.3f s", value }'
}

format_rate() {
  awk -v value="$1" 'BEGIN { printf "%.0f rows/sec", value }'
}

format_bytes() {
  local value="$1"
  if [[ -z "${value}" || "${value}" == "~" || "${value}" == "null" ]]; then
    echo "unavailable"
    return
  fi
  awk -v value="${value}" 'BEGIN { printf "%.1f MiB", value / 1048576 }'
}

run_case() {
  local benchmark_case="$1"
  local config="${ROOT_DIR}/examples/benchmarks/${benchmark_case}/schemaforge.yaml"
  local report="${RESULT_DIR}/${benchmark_case}.yaml"
  local sql="${RESULT_DIR}/${benchmark_case}.sql"
  local log="${RESULT_DIR}/${benchmark_case}.log"

  rm -f "${report}" "${sql}" "${log}"
  echo "Running ${benchmark_case}..."
  if ! "${BINARY}" generate --config "${config}" >"${log}" 2>&1; then
    echo "Benchmark ${benchmark_case} failed."
    cat "${log}"
    return 1
  fi

  local rows generation_time throughput output_size peak_memory sqlite_status postgres_status
  rows="$(yaml_value total_rows "${report}")"
  generation_time="$(yaml_value time_seconds "${report}")"
  throughput="$(yaml_value throughput_rows_per_second "${report}")"
  output_size="$(yaml_value output_file_size_bytes "${report}")"
  peak_memory="$(yaml_value peak_process_memory_bytes "${report}")"
  sqlite_status="$(yaml_value sqlite "${report}")"
  postgres_status="$(yaml_value postgres "${report}")"

  printf '%-24s %s\n' "Rows:" "${rows}"
  printf '%-24s %s\n' "Generation time:" "$(format_seconds "${generation_time}")"
  printf '%-24s %s\n' "Throughput:" "$(format_rate "${throughput}")"
  printf '%-24s %s\n' "Output size:" "$(format_bytes "${output_size}")"
  printf '%-24s %s\n' "Peak process memory:" "$(format_bytes "${peak_memory}")"
  printf '%-24s %s\n' "SQLite validation:" "${sqlite_status}"
  printf '%-24s %s\n' "PostgreSQL validation:" "${postgres_status}"
  printf '%-24s %s\n\n' "Report:" "benchmark-results/${benchmark_case}.yaml"
}

if [[ "${1:-}" == "--help" || "${1:-}" == "-h" ]]; then
  usage
  exit 0
fi

CASES=("${ALL_CASES[@]}")
if [[ "$#" -gt 0 ]]; then
  CASES=("$@")
fi

for benchmark_case in "${CASES[@]}"; do
  if ! is_known_case "${benchmark_case}"; then
    echo "Unknown benchmark case: ${benchmark_case}" >&2
    usage >&2
    exit 1
  fi
done

if [[ ! -x "${BINARY}" ]]; then
  echo "Missing ${BINARY}. Build SchemaForge with: make build" >&2
  exit 1
fi

mkdir -p "${RESULT_DIR}"
cd "${ROOT_DIR}"

for benchmark_case in "${CASES[@]}"; do
  run_case "${benchmark_case}"
done
