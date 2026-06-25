#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BINARY="${ROOT_DIR}/build/schemaforge"
TMP_DIR="$(mktemp -d)"

cleanup() {
  rm -rf "${TMP_DIR}"
}
trap cleanup EXIT

cmake -S "${ROOT_DIR}" -B "${ROOT_DIR}/build" -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build "${ROOT_DIR}/build"

run_copy_validation() {
  local config="${TMP_DIR}/copy.yaml"
  local output="${TMP_DIR}/output.copy.sql"
  local benchmark="${TMP_DIR}/copy-benchmark.yaml"
  local log="${TMP_DIR}/copy.log"

  "${BINARY}" init --schema "${ROOT_DIR}/tests/valid/basic_fk/schema.sql" --config "${config}" \
    >"${log}" 2>&1
  perl -0pi -e "s#file: output\.sql#file: ${output}#" "${config}"
  perl -0pi -e 's/(dialect: )sqlite/${1}postgres/' "${config}"
  perl -0pi -e 's/format: sql/format: postgres_copy/' "${config}"
  perl -0pi -e 's/(sqlite: )true/${1}false/' "${config}"
  perl -0pi -e 's/(postgres: )false/${1}true/' "${config}"
  perl -0pi -e "s#file: benchmark\.yaml#file: ${benchmark}#" "${config}"

  "${BINARY}" generate --config "${config}" >>"${log}" 2>&1
  if grep -q "PostgreSQL Docker validation unavailable" "${log}"; then
    echo "SKIP PostgreSQL validation tests: Docker daemon unavailable"
    exit 0
  fi
  grep -q "PostgreSQL Docker Validation Result: Valid" "${log}"
  grep -q "postgres: passed" "${benchmark}"
  echo "PASS PostgreSQL COPY Docker validation"
}

run_csv_validation() {
  local config="${TMP_DIR}/csv.yaml"
  local output_directory="${TMP_DIR}/csv-output"
  local benchmark="${TMP_DIR}/csv-benchmark.yaml"
  local log="${TMP_DIR}/csv.log"

  "${BINARY}" init --schema "${ROOT_DIR}/tests/valid/basic_fk/schema.sql" --config "${config}" \
    >"${log}" 2>&1
  perl -0pi -e "s#  file: output\.sql\n  format: sql#  directory: ${output_directory}\n  format: csv#" \
    "${config}"
  perl -0pi -e 's/(dialect: )sqlite/${1}postgres/' "${config}"
  perl -0pi -e 's/(sqlite: )true/${1}false/' "${config}"
  perl -0pi -e 's/(postgres: )false/${1}true/' "${config}"
  perl -0pi -e "s#file: benchmark\.yaml#file: ${benchmark}#" "${config}"

  "${BINARY}" generate --config "${config}" >>"${log}" 2>&1
  grep -q "PostgreSQL Docker Validation Result: Valid" "${log}"
  grep -q "postgres: passed" "${benchmark}"
  echo "PASS PostgreSQL CSV Docker validation"
}

cd "${ROOT_DIR}"
run_copy_validation
run_csv_validation
