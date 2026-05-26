#!/usr/bin/env bash
set -u

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BINARY="${ROOT_DIR}/build/schemaforge"
TMP_DIR="$(mktemp -d)"
PASSED=0
FAILED=0

cleanup() {
  rm -rf "${TMP_DIR}"
}
trap cleanup EXIT

build_project() {
  cmake -S "${ROOT_DIR}" -B "${ROOT_DIR}/build" -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
  cmake --build "${ROOT_DIR}/build"
}

record_pass() {
  local name="$1"
  echo "PASS ${name}"
  PASSED=$((PASSED + 1))
}

record_fail() {
  local name="$1"
  local log_file="$2"
  echo "FAIL ${name}"
  echo "----- output -----"
  cat "${log_file}"
  echo "------------------"
  FAILED=$((FAILED + 1))
}

run_valid() {
  local name="$1"
  local schema_path="$2"
  shift 2
  local extra_args=("$@")
  local safe_name="${name//\//_}"
  local log_file="${TMP_DIR}/${safe_name}.log"

  if "${BINARY}" "${ROOT_DIR}/${schema_path}" "${extra_args[@]}" >"${log_file}" 2>&1 &&
      grep -q "SQLite Validation Result: Valid" "${log_file}"; then
    record_pass "${name}"
  else
    record_fail "${name}" "${log_file}"
  fi
}

run_invalid() {
  local name="$1"
  local schema_path="$2"
  local expected_text="$3"
  shift 3
  local extra_args=("$@")
  local safe_name="${name//\//_}"
  local log_file="${TMP_DIR}/${safe_name}.log"

  if "${BINARY}" "${ROOT_DIR}/${schema_path}" "${extra_args[@]}" >"${log_file}" 2>&1; then
    record_fail "${name}" "${log_file}"
    return
  fi

  if grep -q "${expected_text}" "${log_file}"; then
    record_pass "${name}"
  else
    record_fail "${name}" "${log_file}"
  fi
}

run_deterministic() {
  local name="$1"
  local schema_path="$2"
  shift 2
  local extra_args=("$@")
  local safe_name="${name//\//_}"
  local first_log="${TMP_DIR}/${safe_name}_a.log"
  local second_log="${TMP_DIR}/${safe_name}_b.log"

  if ! "${BINARY}" "${ROOT_DIR}/${schema_path}" "${extra_args[@]}" >"${first_log}" 2>&1; then
    record_fail "${name}" "${first_log}"
    return
  fi

  if ! "${BINARY}" "${ROOT_DIR}/${schema_path}" "${extra_args[@]}" >"${second_log}" 2>&1; then
    record_fail "${name}" "${second_log}"
    return
  fi

  if cmp -s "${first_log}" "${second_log}"; then
    record_pass "${name}"
  else
    echo "FAIL ${name}"
    echo "----- first output -----"
    cat "${first_log}"
    echo "----- second output -----"
    cat "${second_log}"
    echo "------------------------"
    FAILED=$((FAILED + 1))
  fi
}

cd "${ROOT_DIR}" || exit 1
build_project

run_valid "valid/basic_fk" "tests/valid/basic_fk/schema.sql"
run_valid "valid/reverse_table_order" "tests/valid/reverse_table_order/schema.sql"
run_valid "valid/single_table_random_values" "tests/valid/single_table_random_values/schema.sql"
run_valid "valid/unique_text" "tests/valid/unique_text/schema.sql"
run_valid "valid/unique_fk_within_parent_capacity" "tests/valid/unique_fk_within_parent_capacity/schema.sql"
run_valid "valid/unique_boolean_within_capacity" "tests/valid/unique_boolean_within_capacity/schema.sql" --rows flags=2
run_valid "valid/dependency_chain" "tests/valid/dependency_chain/schema.sql"
run_deterministic "valid/deterministic_output" "tests/valid/basic_fk/schema.sql" --seed 42

run_invalid "invalid/missing_fk_table" "tests/invalid/missing_fk_table/schema.sql" "Referenced table 'users' not found"
run_invalid "invalid/missing_fk_column" "tests/invalid/missing_fk_column/schema.sql" "Referenced column 'user_id' not found"
run_invalid "invalid/fk_type_mismatch" "tests/invalid/fk_type_mismatch/schema.sql" "Referenced foreign key column 'users.id' must use INT or BIGINT"
run_invalid "invalid/unsupported_date" "tests/invalid/unsupported_date/schema.sql" "unsupported generation type"
run_invalid "invalid/unique_boolean_too_many" "tests/invalid/unique_boolean_too_many/schema.sql" "UNIQUE BOOLEAN"
run_invalid "invalid/unique_fk_too_many_children" "tests/invalid/unique_fk_too_many_children/schema.sql" "UNIQUE foreign key"
run_invalid "invalid/zero_parent_rows_for_fk" "tests/invalid/zero_parent_rows_for_fk/schema.sql" "references table 'users', but that table has 0 rows" --rows users=0 --rows orders=5

echo
echo "Passed: ${PASSED}"
echo "Failed: ${FAILED}"

if [[ "${FAILED}" -ne 0 ]]; then
  exit 1
fi
