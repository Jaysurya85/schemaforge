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
  local safe_name="${name//\//_}"
  local config_file="${TMP_DIR}/${safe_name}.yaml"
  local output_file="${TMP_DIR}/${safe_name}.sql"
  local benchmark_file="${TMP_DIR}/${safe_name}_benchmark.yaml"
  local init_log="${TMP_DIR}/${safe_name}_init.log"
  local generate_log="${TMP_DIR}/${safe_name}_generate.log"

  if ! "${BINARY}" init --schema "${ROOT_DIR}/${schema_path}" --config "${config_file}" \
      >"${init_log}" 2>&1; then
    record_fail "${name}" "${init_log}"
    return
  fi

  perl -0pi -e "s#file: output\\.sql#file: ${output_file}#" "${config_file}"
  perl -0pi -e "s#file: benchmark\\.yaml#file: ${benchmark_file}#" "${config_file}"
  while [[ "$#" -gt 0 ]]; do
    case "$1" in
      --rows)
        local table="${2%%=*}"
        local rows="${2#*=}"
        perl -0pi -e "s/(${table}:\\n\\s+rows: )\\d+/\${1}${rows}/" "${config_file}"
        shift 2
        ;;
      *)
        echo "Unknown test option: $1" >"${generate_log}"
        record_fail "${name}" "${generate_log}"
        return
        ;;
    esac
  done

  if "${BINARY}" generate --config "${config_file}" >"${generate_log}" 2>&1 &&
      grep -q "SQLite Validation Result: Valid" "${generate_log}" &&
      grep -q "sqlite: passed" "${benchmark_file}" &&
      grep -q "total_rows:" "${benchmark_file}" &&
      ! grep -q "Generated table data" "${generate_log}" &&
      [[ -s "${output_file}" ]] &&
      [[ -s "${benchmark_file}" ]]; then
    record_pass "${name}"
  else
    record_fail "${name}" "${generate_log}"
  fi
}

run_invalid() {
  local name="$1"
  local schema_path="$2"
  local expected_text="$3"
  shift 3
  local safe_name="${name//\//_}"
  local config_file="${TMP_DIR}/${safe_name}.yaml"
  local output_file="${TMP_DIR}/${safe_name}.sql"
  local benchmark_file="${TMP_DIR}/${safe_name}_benchmark.yaml"
  local init_log="${TMP_DIR}/${safe_name}_init.log"
  local generate_log="${TMP_DIR}/${safe_name}_generate.log"

  if ! "${BINARY}" init --schema "${ROOT_DIR}/${schema_path}" --config "${config_file}" \
      >"${init_log}" 2>&1; then
    if grep -q "${expected_text}" "${init_log}"; then
      record_pass "${name}"
    else
      record_fail "${name}" "${init_log}"
    fi
    return
  fi

  perl -0pi -e "s#file: output\\.sql#file: ${output_file}#" "${config_file}"
  perl -0pi -e "s#file: benchmark\\.yaml#file: ${benchmark_file}#" "${config_file}"
  while [[ "$#" -gt 0 ]]; do
    case "$1" in
      --rows)
        local table="${2%%=*}"
        local rows="${2#*=}"
        perl -0pi -e "s/(${table}:\\n\\s+rows: )\\d+/\${1}${rows}/" "${config_file}"
        shift 2
        ;;
      *)
        echo "Unknown test option: $1" >"${generate_log}"
        record_fail "${name}" "${generate_log}"
        return
        ;;
    esac
  done

  if "${BINARY}" generate --config "${config_file}" >"${generate_log}" 2>&1; then
    record_fail "${name}" "${generate_log}"
    return
  fi

  if grep -q "${expected_text}" "${generate_log}"; then
    record_pass "${name}"
  else
    record_fail "${name}" "${generate_log}"
  fi
}

run_deterministic() {
  local name="$1"
  local schema_path="$2"
  shift 2
  local safe_name="${name//\//_}"
  local config_file="${TMP_DIR}/${safe_name}.yaml"
  local first_log="${TMP_DIR}/${safe_name}_a.log"
  local second_log="${TMP_DIR}/${safe_name}_b.log"
  local first_output="${TMP_DIR}/${safe_name}_a.sql"
  local second_output="${TMP_DIR}/${safe_name}_b.sql"
  local first_benchmark="${TMP_DIR}/${safe_name}_a_benchmark.yaml"
  local second_benchmark="${TMP_DIR}/${safe_name}_b_benchmark.yaml"

  if ! "${BINARY}" init --schema "${ROOT_DIR}/${schema_path}" --config "${config_file}" \
      >"${first_log}" 2>&1; then
    record_fail "${name}" "${first_log}"
    return
  fi

  while [[ "$#" -gt 0 ]]; do
    case "$1" in
      --seed)
        perl -0pi -e "s/(seed: )\\d+/\${1}${2}/" "${config_file}"
        shift 2
        ;;
      *)
        echo "Unknown test option: $1" >"${first_log}"
        record_fail "${name}" "${first_log}"
        return
        ;;
    esac
  done

  perl -0pi -e "s#file: output\\.sql#file: ${first_output}#" "${config_file}"
  perl -0pi -e "s#file: benchmark\\.yaml#file: ${first_benchmark}#" "${config_file}"
  if ! "${BINARY}" generate --config "${config_file}" >"${first_log}" 2>&1; then
    record_fail "${name}" "${first_log}"
    return
  fi

  perl -0pi -e "s#file: ${first_output}#file: ${second_output}#" "${config_file}"
  perl -0pi -e "s#file: ${first_benchmark}#file: ${second_benchmark}#" "${config_file}"
  if ! "${BINARY}" generate --config "${config_file}" >"${second_log}" 2>&1; then
    record_fail "${name}" "${second_log}"
    return
  fi

  if cmp -s "${first_output}" "${second_output}"; then
    record_pass "${name}"
  else
    echo "FAIL ${name}"
    echo "----- first SQL -----"
    cat "${first_output}"
    echo "----- second SQL -----"
    cat "${second_output}"
    echo "------------------------"
    FAILED=$((FAILED + 1))
  fi
}

run_usage_invalid() {
  local name="cli/no_command"
  local log_file="${TMP_DIR}/cli_no_command.log"
  if "${BINARY}" >"${log_file}" 2>&1; then
    record_fail "${name}" "${log_file}"
    return
  fi

  if grep -q "Usage:" "${log_file}"; then
    record_pass "${name}"
  else
    record_fail "${name}" "${log_file}"
  fi
}

run_sqlite_disabled() {
  local name="valid/sqlite_disabled"
  local config_file="${TMP_DIR}/sqlite_disabled.yaml"
  local output_file="${TMP_DIR}/sqlite_disabled.sql"
  local benchmark_file="${TMP_DIR}/sqlite_disabled_benchmark.yaml"
  local log_file="${TMP_DIR}/sqlite_disabled.log"

  if ! "${BINARY}" init --schema "${ROOT_DIR}/tests/valid/basic_fk/schema.sql" \
      --config "${config_file}" >"${log_file}" 2>&1; then
    record_fail "${name}" "${log_file}"
    return
  fi

  perl -0pi -e "s#file: output\\.sql#file: ${output_file}#" "${config_file}"
  perl -0pi -e "s#file: benchmark\\.yaml#file: ${benchmark_file}#" "${config_file}"
  perl -0pi -e "s/(sqlite: )true/\${1}false/" "${config_file}"

  if "${BINARY}" generate --config "${config_file}" >"${log_file}" 2>&1 &&
      grep -q "Wrote SQL INSERT statements" "${log_file}" &&
      grep -q "sqlite: skipped" "${benchmark_file}" &&
      ! grep -q "SQLite Validation Result" "${log_file}" &&
      [[ -s "${output_file}" ]] &&
      [[ -s "${benchmark_file}" ]]; then
    record_pass "${name}"
  else
    record_fail "${name}" "${log_file}"
  fi
}

cd "${ROOT_DIR}" || exit 1
build_project

run_usage_invalid
run_valid "valid/basic_fk" "tests/valid/basic_fk/schema.sql"
run_valid "valid/reverse_table_order" "tests/valid/reverse_table_order/schema.sql"
run_valid "valid/single_table_random_values" "tests/valid/single_table_random_values/schema.sql"
run_valid "valid/unique_text" "tests/valid/unique_text/schema.sql"
run_valid "valid/unique_fk_within_parent_capacity" "tests/valid/unique_fk_within_parent_capacity/schema.sql"
run_valid "valid/unique_boolean_within_capacity" "tests/valid/unique_boolean_within_capacity/schema.sql" --rows flags=2
run_valid "valid/dependency_chain" "tests/valid/dependency_chain/schema.sql"
run_deterministic "valid/deterministic_output" "tests/valid/basic_fk/schema.sql" --seed 42
run_sqlite_disabled

run_invalid "invalid/missing_fk_table" "tests/invalid/missing_fk_table/schema.sql" "Referenced table 'users' not found"
run_invalid "invalid/missing_fk_column" "tests/invalid/missing_fk_column/schema.sql" "Referenced column 'user_id' not found"
run_invalid "invalid/fk_type_mismatch" "tests/invalid/fk_type_mismatch/schema.sql" "Referenced foreign key column 'users.id' must use INT or BIGINT"
run_invalid "invalid/unsupported_date" "tests/invalid/unsupported_date/schema.sql" "unsupported generation type"
run_invalid "invalid/unique_boolean_too_many" "tests/invalid/unique_boolean_too_many/schema.sql" "UNIQUE BOOLEAN"
run_invalid "invalid/unique_fk_too_many_children" "tests/invalid/unique_fk_too_many_children/schema.sql" "UNIQUE foreign key" --rows orders=25
run_invalid "invalid/zero_parent_rows_for_fk" "tests/invalid/zero_parent_rows_for_fk/schema.sql" "references table 'users', but that table has 0 rows" --rows users=0 --rows orders=5

echo
echo "Passed: ${PASSED}"
echo "Failed: ${FAILED}"

if [[ "${FAILED}" -ne 0 ]]; then
  exit 1
fi
