#!/usr/bin/env bash
set -u

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BINARY="${ROOT_DIR}/build/schemaforge"
TMP_DIR="$(mktemp -d)"
ARTIFACT_DIR="${ROOT_DIR}/tests/artifacts"
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

prepare_artifacts() {
  rm -rf "${ARTIFACT_DIR}"
  mkdir -p "${ARTIFACT_DIR}"
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

benchmark_metrics_valid() {
  local output_file="$1"
  local benchmark_file="$2"
  local generate_log="$3"
  local output_size
  output_size="$(wc -c <"${output_file}")"

  if ! grep -q "output_file_size_bytes: ${output_size}" "${benchmark_file}" ||
      ! grep -q "Output file size: .* MiB" "${generate_log}"; then
    return 1
  fi

  if [[ "$(uname -s)" == "Linux" ]]; then
    grep -Eq "peak_process_memory_bytes: [1-9][0-9]*" "${benchmark_file}" &&
      grep -q "Peak process memory usage: .* MiB" "${generate_log}"
  else
    grep -Eq "peak_process_memory_bytes: (~|null)" "${benchmark_file}" &&
      grep -q "Peak process memory usage: unavailable" "${generate_log}"
  fi
}

run_valid() {
  local name="$1"
  local schema_path="$2"
  shift 2
  local safe_name="${name//\//_}"
  local config_file="${TMP_DIR}/${safe_name}.yaml"
  local output_file="${ARTIFACT_DIR}/${safe_name}.sql"
  local benchmark_file="${ARTIFACT_DIR}/${safe_name}_benchmark.yaml"
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
      benchmark_metrics_valid "${output_file}" "${benchmark_file}" "${generate_log}" &&
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

run_invalid_contains_all() {
  local name="$1"
  local schema_path="$2"
  shift 2
  local safe_name="${name//\//_}"
  local config_file="${TMP_DIR}/${safe_name}.yaml"
  local log_file="${TMP_DIR}/${safe_name}.log"

  if "${BINARY}" init --schema "${ROOT_DIR}/${schema_path}" --config "${config_file}" \
      >"${log_file}" 2>&1; then
    record_fail "${name}" "${log_file}"
    return
  fi

  for expected_text in "$@"; do
    if ! grep -q "${expected_text}" "${log_file}"; then
      record_fail "${name}" "${log_file}"
      return
    fi
  done

  record_pass "${name}"
}

run_config_unknown_table() {
  local name="invalid/config_unknown_table"
  local config_file="${TMP_DIR}/config_unknown_table.yaml"
  local output_file="${TMP_DIR}/config_unknown_table.sql"
  local benchmark_file="${TMP_DIR}/config_unknown_table_benchmark.yaml"
  local log_file="${TMP_DIR}/config_unknown_table.log"

  if ! "${BINARY}" init --schema "${ROOT_DIR}/tests/valid/basic_fk/schema.sql" \
      --config "${config_file}" >"${log_file}" 2>&1; then
    record_fail "${name}" "${log_file}"
    return
  fi

  perl -0pi -e "s#file: output\\.sql#file: ${output_file}#" "${config_file}"
  perl -0pi -e "s#file: benchmark\\.yaml#file: ${benchmark_file}#" "${config_file}"
  printf "  ghost_table:\n    rows: 1\n" >>"${config_file}"

  if "${BINARY}" generate --config "${config_file}" >"${log_file}" 2>&1; then
    record_fail "${name}" "${log_file}"
    return
  fi

  if grep -q "Config contains unknown table 'ghost_table'" "${log_file}"; then
    record_pass "${name}"
  else
    record_fail "${name}" "${log_file}"
  fi
}

run_missing_schema_path() {
  local name="invalid/missing_schema_path"
  local config_file="${TMP_DIR}/missing_schema_path.yaml"
  local log_file="${TMP_DIR}/missing_schema_path.log"

  cat >"${config_file}" <<EOF
schema: ""
generation:
  seed: 42
  default_rows: 10
tables: {}
EOF

  if "${BINARY}" generate --config "${config_file}" >"${log_file}" 2>&1; then
    record_fail "${name}" "${log_file}"
    return
  fi

  if grep -q "Missing schema path" "${log_file}"; then
    record_pass "${name}"
  else
    record_fail "${name}" "${log_file}"
  fi
}

run_missing_schema_file() {
  local name="invalid/missing_schema_file"
  local config_file="${TMP_DIR}/missing_schema_file.yaml"
  local log_file="${TMP_DIR}/missing_schema_file.log"

  cat >"${config_file}" <<EOF
schema: ${TMP_DIR}/does_not_exist.sql
generation:
  seed: 42
  default_rows: 10
tables: {}
EOF

  if "${BINARY}" generate --config "${config_file}" >"${log_file}" 2>&1; then
    record_fail "${name}" "${log_file}"
    return
  fi

  if grep -q "Missing schema file" "${log_file}"; then
    record_pass "${name}"
  else
    record_fail "${name}" "${log_file}"
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
  local first_output="${ARTIFACT_DIR}/${safe_name}_a.sql"
  local second_output="${ARTIFACT_DIR}/${safe_name}_b.sql"
  local first_benchmark="${ARTIFACT_DIR}/${safe_name}_a_benchmark.yaml"
  local second_benchmark="${ARTIFACT_DIR}/${safe_name}_b_benchmark.yaml"

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
  local output_file="${ARTIFACT_DIR}/valid_sqlite_disabled.sql"
  local benchmark_file="${ARTIFACT_DIR}/valid_sqlite_disabled_benchmark.yaml"
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

run_benchmark_example_smoke() {
  local benchmark_case="$1"
  local expected_sqlite_status="$2"
  local expected_rows="$3"
  local name="benchmark/${benchmark_case}_smoke"
  local source_config="${ROOT_DIR}/examples/benchmarks/${benchmark_case}/schemaforge.yaml"
  local config_file="${TMP_DIR}/${benchmark_case}_smoke.yaml"
  local output_file="${TMP_DIR}/${benchmark_case}_smoke.sql"
  local benchmark_file="${TMP_DIR}/${benchmark_case}_smoke_benchmark.yaml"
  local log_file="${TMP_DIR}/${benchmark_case}_smoke.log"

  cp "${source_config}" "${config_file}"
  perl -0pi -e 's/(rows: )\d+/${1}10/g' "${config_file}"
  perl -0pi -e "s#file: benchmark-results/${benchmark_case}\.sql#file: ${output_file}#" \
    "${config_file}"
  perl -0pi -e "s#file: benchmark-results/${benchmark_case}\.yaml#file: ${benchmark_file}#" \
    "${config_file}"

  if "${BINARY}" generate --config "${config_file}" >"${log_file}" 2>&1 &&
      grep -q "total_rows: ${expected_rows}" "${benchmark_file}" &&
      grep -q "throughput_rows_per_second:" "${benchmark_file}" &&
      grep -q "sqlite: ${expected_sqlite_status}" "${benchmark_file}" &&
      benchmark_metrics_valid "${output_file}" "${benchmark_file}" "${log_file}"; then
    record_pass "${name}"
  else
    record_fail "${name}" "${log_file}"
  fi
}

run_benchmark_unknown_case() {
  local name="benchmark/unknown_case"
  local log_file="${TMP_DIR}/benchmark_unknown_case.log"

  if "${ROOT_DIR}/scripts/run_benchmarks.sh" unknown_case >"${log_file}" 2>&1; then
    record_fail "${name}" "${log_file}"
    return
  fi

  if grep -q "Unknown benchmark case: unknown_case" "${log_file}"; then
    record_pass "${name}"
  else
    record_fail "${name}" "${log_file}"
  fi
}

configure_csv_output() {
  local config_file="$1"
  local output_directory="$2"
  local benchmark_file="$3"

  perl -0pi -e "s#  file: output\.sql\n  format: sql#  directory: ${output_directory}\n  format: csv#" \
    "${config_file}"
  perl -0pi -e "s#file: benchmark\.yaml#file: ${benchmark_file}#" "${config_file}"
}

csv_metrics_valid() {
  local benchmark_file="$1"
  shift
  local expected_size=0
  local csv_file
  for csv_file in "$@"; do
    expected_size=$((expected_size + $(wc -c <"${csv_file}")))
  done
  grep -q "output_file_size_bytes: ${expected_size}" "${benchmark_file}"
}

run_csv_single_fk() {
  local name="valid/csv_single_fk"
  local config_file="${TMP_DIR}/csv_single_fk.yaml"
  local output_directory="${TMP_DIR}/csv/single/nested"
  local benchmark_file="${TMP_DIR}/csv_single_fk_benchmark.yaml"
  local log_file="${TMP_DIR}/csv_single_fk.log"
  local users_file="${output_directory}/users.csv"
  local orders_file="${output_directory}/orders.csv"

  if ! "${BINARY}" init --schema "${ROOT_DIR}/tests/valid/basic_fk/schema.sql" \
      --config "${config_file}" >"${log_file}" 2>&1; then
    record_fail "${name}" "${log_file}"
    return
  fi
  configure_csv_output "${config_file}" "${output_directory}" "${benchmark_file}"

  if "${BINARY}" generate --config "${config_file}" >"${log_file}" 2>&1 &&
      [[ "$(head -n 1 "${users_file}")" == "id,name,email" ]] &&
      [[ "$(head -n 1 "${orders_file}")" == "id,user_id" ]] &&
      [[ "$(wc -l <"${users_file}")" -eq 11 ]] &&
      [[ "$(wc -l <"${orders_file}")" -eq 11 ]] &&
      awk -F, 'FNR > 1 && $1 != FNR - 1 { exit 1 }' "${users_file}" "${orders_file}" &&
      awk -F, 'NR == FNR { if (NR > 1) ids[$1] = 1; next }
                  FNR > 1 && !($2 in ids) { exit 1 }' "${users_file}" "${orders_file}" &&
      grep -q "SQLite validation skipped for CSV output" "${log_file}" &&
      grep -q "sqlite: skipped" "${benchmark_file}" &&
      csv_metrics_valid "${benchmark_file}" "${users_file}" "${orders_file}"; then
    record_pass "${name}"
  else
    record_fail "${name}" "${log_file}"
  fi
}

run_csv_composite_fk() {
  local name="valid/csv_composite_fk"
  local config_file="${TMP_DIR}/csv_composite_fk.yaml"
  local output_directory="${TMP_DIR}/csv/composite"
  local benchmark_file="${TMP_DIR}/csv_composite_fk_benchmark.yaml"
  local log_file="${TMP_DIR}/csv_composite_fk.log"
  local parent_file="${output_directory}/memberships.csv"
  local child_file="${output_directory}/membership_events.csv"

  if ! "${BINARY}" init --schema "${ROOT_DIR}/tests/valid/composite_fk_primary_key/schema.sql" \
      --config "${config_file}" >"${log_file}" 2>&1; then
    record_fail "${name}" "${log_file}"
    return
  fi
  configure_csv_output "${config_file}" "${output_directory}" "${benchmark_file}"
  perl -0pi -e 's/(memberships:\n\s+rows: )\d+/${1}4/' "${config_file}"
  perl -0pi -e 's/(membership_events:\n\s+rows: )\d+/${1}4/' "${config_file}"

  if "${BINARY}" generate --config "${config_file}" >"${log_file}" 2>&1 &&
      [[ "$(head -n 1 "${parent_file}")" == "user_id,team_id" ]] &&
      [[ "$(head -n 1 "${child_file}")" == "id,user_id,team_id" ]] &&
      awk -F, 'FNR > 1 && $1 != FNR - 1 { exit 1 }' "${child_file}" &&
      awk -F, 'NR == FNR { if (NR > 1) keys[$1 SUBSEP $2] = 1; next }
                  FNR > 1 && !(($2 SUBSEP $3) in keys) { exit 1 }' \
        "${parent_file}" "${child_file}" &&
      csv_metrics_valid "${benchmark_file}" "${parent_file}" "${child_file}"; then
    record_pass "${name}"
  else
    record_fail "${name}" "${log_file}"
  fi
}

run_csv_zero_rows() {
  local name="valid/csv_zero_rows"
  local config_file="${TMP_DIR}/csv_zero_rows.yaml"
  local output_directory="${TMP_DIR}/csv/zero"
  local benchmark_file="${TMP_DIR}/csv_zero_rows_benchmark.yaml"
  local log_file="${TMP_DIR}/csv_zero_rows.log"

  if ! "${BINARY}" init --schema "${ROOT_DIR}/tests/valid/basic_fk/schema.sql" \
      --config "${config_file}" >"${log_file}" 2>&1; then
    record_fail "${name}" "${log_file}"
    return
  fi
  configure_csv_output "${config_file}" "${output_directory}" "${benchmark_file}"
  perl -0pi -e 's/(rows: )\d+/${1}0/g' "${config_file}"

  if "${BINARY}" generate --config "${config_file}" >"${log_file}" 2>&1 &&
      [[ "$(cat "${output_directory}/users.csv")" == "id,name,email" ]] &&
      [[ "$(cat "${output_directory}/orders.csv")" == "id,user_id" ]]; then
    record_pass "${name}"
  else
    record_fail "${name}" "${log_file}"
  fi
}

run_csv_missing_directory() {
  local name="invalid/csv_missing_directory"
  local config_file="${TMP_DIR}/csv_missing_directory.yaml"
  local log_file="${TMP_DIR}/csv_missing_directory.log"

  if ! "${BINARY}" init --schema "${ROOT_DIR}/tests/valid/basic_fk/schema.sql" \
      --config "${config_file}" >"${log_file}" 2>&1; then
    record_fail "${name}" "${log_file}"
    return
  fi
  perl -0pi -e 's/format: sql/format: csv/' "${config_file}"

  if "${BINARY}" generate --config "${config_file}" >"${log_file}" 2>&1; then
    record_fail "${name}" "${log_file}"
    return
  fi
  if grep -q "output.directory is required for CSV output" "${log_file}"; then
    record_pass "${name}"
  else
    record_fail "${name}" "${log_file}"
  fi
}

configure_postgres_copy_output() {
  local config_file="$1"
  local output_file="$2"
  local benchmark_file="$3"

  perl -0pi -e "s#file: output\.sql#file: ${output_file}#" "${config_file}"
  perl -0pi -e 's/format: sql/format: postgres_copy/' "${config_file}"
  perl -0pi -e "s#file: benchmark\.yaml#file: ${benchmark_file}#" "${config_file}"
}

postgres_copy_fk_values_valid() {
  local output_file="$1"
  awk -F '\t' '
    /^COPY "users" / { section = "users"; next }
    /^COPY "orders" / { section = "orders"; next }
    /^\\\.$/ { section = ""; next }
    section == "users" { ids[$1] = 1; next }
    section == "orders" && !($2 in ids) { exit 1 }
  ' "${output_file}"
}

run_postgres_copy_single_fk() {
  local name="valid/postgres_copy_single_fk"
  local config_file="${TMP_DIR}/postgres_copy_single_fk.yaml"
  local first_output="${TMP_DIR}/postgres_copy_single_fk_a.sql"
  local second_output="${TMP_DIR}/postgres_copy_single_fk_b.sql"
  local first_benchmark="${TMP_DIR}/postgres_copy_single_fk_a_benchmark.yaml"
  local second_benchmark="${TMP_DIR}/postgres_copy_single_fk_b_benchmark.yaml"
  local log_file="${TMP_DIR}/postgres_copy_single_fk.log"

  if ! "${BINARY}" init --schema "${ROOT_DIR}/tests/valid/basic_fk/schema.sql" \
      --config "${config_file}" >"${log_file}" 2>&1; then
    record_fail "${name}" "${log_file}"
    return
  fi
  configure_postgres_copy_output "${config_file}" "${first_output}" "${first_benchmark}"

  if ! "${BINARY}" generate --config "${config_file}" >"${log_file}" 2>&1; then
    record_fail "${name}" "${log_file}"
    return
  fi

  perl -0pi -e "s#file: ${first_output}#file: ${second_output}#" "${config_file}"
  perl -0pi -e "s#file: ${first_benchmark}#file: ${second_benchmark}#" "${config_file}"
  if "${BINARY}" generate --config "${config_file}" >>"${log_file}" 2>&1 &&
      [[ "$(head -n 1 "${first_output}")" == "BEGIN;" ]] &&
      [[ "$(tail -n 1 "${first_output}")" == "COMMIT;" ]] &&
      grep -q '^COPY "users" ("id", "name", "email") FROM STDIN WITH (FORMAT text);$' \
        "${first_output}" &&
      grep -q '^COPY "orders" ("id", "user_id") FROM STDIN WITH (FORMAT text);$' \
        "${first_output}" &&
      [[ "$(grep -c '^\\\.$' "${first_output}")" -eq 2 ]] &&
      postgres_copy_fk_values_valid "${first_output}" &&
      cmp -s "${first_output}" "${second_output}" &&
      grep -q "SQLite validation skipped for PostgreSQL COPY output" "${log_file}" &&
      grep -q "sqlite: skipped" "${first_benchmark}" &&
      benchmark_metrics_valid "${first_output}" "${first_benchmark}" "${log_file}"; then
    record_pass "${name}"
  else
    record_fail "${name}" "${log_file}"
  fi
}

run_postgres_copy_composite_fk() {
  local name="valid/postgres_copy_composite_fk"
  local config_file="${TMP_DIR}/postgres_copy_composite_fk.yaml"
  local output_file="${TMP_DIR}/postgres_copy_composite_fk.sql"
  local benchmark_file="${TMP_DIR}/postgres_copy_composite_fk_benchmark.yaml"
  local log_file="${TMP_DIR}/postgres_copy_composite_fk.log"

  if ! "${BINARY}" init --schema "${ROOT_DIR}/tests/valid/composite_fk_primary_key/schema.sql" \
      --config "${config_file}" >"${log_file}" 2>&1; then
    record_fail "${name}" "${log_file}"
    return
  fi
  configure_postgres_copy_output "${config_file}" "${output_file}" "${benchmark_file}"
  perl -0pi -e 's/(memberships:\n\s+rows: )\d+/${1}4/' "${config_file}"
  perl -0pi -e 's/(membership_events:\n\s+rows: )\d+/${1}4/' "${config_file}"

  if "${BINARY}" generate --config "${config_file}" >"${log_file}" 2>&1 &&
      awk -F '\t' '
        /^COPY "memberships" / { section = "parent"; next }
        /^COPY "membership_events" / { section = "child"; next }
        /^\\\.$/ { section = ""; next }
        section == "parent" { keys[$1 SUBSEP $2] = 1; next }
        section == "child" && !(($2 SUBSEP $3) in keys) { exit 1 }
      ' "${output_file}" &&
      benchmark_metrics_valid "${output_file}" "${benchmark_file}" "${log_file}"; then
    record_pass "${name}"
  else
    record_fail "${name}" "${log_file}"
  fi
}

run_postgres_copy_zero_rows() {
  local name="valid/postgres_copy_zero_rows"
  local config_file="${TMP_DIR}/postgres_copy_zero_rows.yaml"
  local output_file="${TMP_DIR}/postgres_copy_zero_rows.sql"
  local benchmark_file="${TMP_DIR}/postgres_copy_zero_rows_benchmark.yaml"
  local log_file="${TMP_DIR}/postgres_copy_zero_rows.log"

  if ! "${BINARY}" init --schema "${ROOT_DIR}/tests/valid/basic_fk/schema.sql" \
      --config "${config_file}" >"${log_file}" 2>&1; then
    record_fail "${name}" "${log_file}"
    return
  fi
  configure_postgres_copy_output "${config_file}" "${output_file}" "${benchmark_file}"
  perl -0pi -e 's/(rows: )\d+/${1}0/g' "${config_file}"

  if "${BINARY}" generate --config "${config_file}" >"${log_file}" 2>&1 &&
      [[ "$(grep -c '^COPY ' "${output_file}")" -eq 2 ]] &&
      [[ "$(grep -c '^\\\.$' "${output_file}")" -eq 2 ]] &&
      [[ "$(wc -l <"${output_file}")" -eq 6 ]]; then
    record_pass "${name}"
  else
    record_fail "${name}" "${log_file}"
  fi
}

run_postgres_docker_unavailable() {
  local name="valid/postgres_docker_unavailable"
  local config_file="${TMP_DIR}/postgres_docker_unavailable.yaml"
  local output_file="${TMP_DIR}/postgres_docker_unavailable.sql"
  local benchmark_file="${TMP_DIR}/postgres_docker_unavailable_benchmark.yaml"
  local log_file="${TMP_DIR}/postgres_docker_unavailable.log"
  local fake_bin="${TMP_DIR}/fake-docker-bin"

  if ! "${BINARY}" init --schema "${ROOT_DIR}/tests/valid/basic_fk/schema.sql" \
      --config "${config_file}" >"${log_file}" 2>&1; then
    record_fail "${name}" "${log_file}"
    return
  fi
  configure_postgres_copy_output "${config_file}" "${output_file}" "${benchmark_file}"
  perl -0pi -e 's/(postgres: )false/${1}true/' "${config_file}"
  mkdir -p "${fake_bin}"
  printf '#!/usr/bin/env sh\necho "fake Docker daemon unavailable" >&2\nexit 1\n' \
    >"${fake_bin}/docker"
  chmod +x "${fake_bin}/docker"

  if PATH="${fake_bin}:${PATH}" "${BINARY}" generate --config "${config_file}" \
      >"${log_file}" 2>&1 &&
      grep -q "PostgreSQL Docker validation unavailable" "${log_file}" &&
      grep -q "postgres: unavailable" "${benchmark_file}"; then
    record_pass "${name}"
  else
    record_fail "${name}" "${log_file}"
  fi
}

run_postgres_validation_requires_supported_output() {
  local name="invalid/postgres_validation_output_format"
  local config_file="${TMP_DIR}/postgres_validation_output_format.yaml"
  local log_file="${TMP_DIR}/postgres_validation_output_format.log"

  if ! "${BINARY}" init --schema "${ROOT_DIR}/tests/valid/basic_fk/schema.sql" \
      --config "${config_file}" >"${log_file}" 2>&1; then
    record_fail "${name}" "${log_file}"
    return
  fi
  perl -0pi -e 's/(postgres: )false/${1}true/' "${config_file}"

  if "${BINARY}" generate --config "${config_file}" >"${log_file}" 2>&1; then
    record_fail "${name}" "${log_file}"
    return
  fi
  if grep -q "PostgreSQL validation requires CSV or postgres_copy output" "${log_file}"; then
    record_pass "${name}"
  else
    record_fail "${name}" "${log_file}"
  fi
}

run_csv_literal_formatting() {
  local name="unit/csv_literal_formatting"
  local source_file="${TMP_DIR}/csv_literal_formatting.cpp"
  local binary_file="${TMP_DIR}/csv_literal_formatting"
  local log_file="${TMP_DIR}/csv_literal_formatting.log"
  local unsafe_directory="${TMP_DIR}/csv_unsafe"

  cat >"${source_file}" <<'EOF'
#include <iostream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "schemaforge/output/CsvWriter.h"
#include "schemaforge/schema/Column.h"
#include "schemaforge/schema/Table.h"

int main(int argc, char* argv[]) {
  using namespace schemaforge;
  if (argc != 2) {
    return 1;
  }

  std::vector<ColumnPtr> columns;
  columns.push_back(std::make_unique<Column>("id", ColumnType{.data_type = DataType::INT}));
  columns.push_back(std::make_unique<Column>("note", ColumnType{.data_type = DataType::TEXT}));
  columns.push_back(std::make_unique<Column>("empty_text", ColumnType{.data_type = DataType::TEXT}));
  columns.push_back(std::make_unique<Column>("missing", ColumnType{.data_type = DataType::TEXT}));
  columns.push_back(std::make_unique<Column>("active", ColumnType{.data_type = DataType::BOOLEAN}));
  columns.push_back(std::make_unique<Column>("amount", ColumnType{.data_type = DataType::DECIMAL}));
  columns.push_back(std::make_unique<Column>("born_on", ColumnType{.data_type = DataType::DATE}));
  columns.push_back(std::make_unique<Column>("alarm", ColumnType{.data_type = DataType::TIME}));
  columns.push_back(
      std::make_unique<Column>("starts_at", ColumnType{.data_type = DataType::DATETIME}));

  std::vector<const Column*> row_columns;
  for (const auto& column : columns) {
    row_columns.push_back(column.get());
  }
  Table table("samples", std::move(columns), {}, {}, {});
  GeneratedRow row{
      .table = &table,
      .columns = std::move(row_columns),
      .values = {GeneratedValue::integer(7), GeneratedValue::text("hello, \"world\"\nnext"),
                 GeneratedValue::text(""), GeneratedValue::null(),
                 GeneratedValue::boolean(true), GeneratedValue::numeric(12.5),
                 GeneratedValue::date(DateValue{2026, 1, 1}),
                 GeneratedValue::time(TimeValue{12, 30, 0}),
                 GeneratedValue::date_time(
                     DateTimeValue{DateValue{2026, 1, 1}, TimeValue{12, 30, 0}})},
  };

  std::ostringstream output;
  CsvWriter::write_header(output, table);
  CsvWriter::write_row(output, row);
  const std::string expected =
      "id,note,empty_text,missing,active,amount,born_on,alarm,starts_at\n"
      "7,\"hello, \"\"world\"\"\nnext\",\"\",,true,12.50,2026-01-01,12:30:00,"
      "2026-01-01 12:30:00\n";
  if (output.str() != expected) {
    std::cerr << "Expected:\n" << expected << "Actual:\n" << output.str();
    return 1;
  }

  std::vector<ColumnPtr> unsafe_columns;
  unsafe_columns.push_back(
      std::make_unique<Column>("id", ColumnType{.data_type = DataType::INT}));
  std::vector<TablePtr> unsafe_tables;
  unsafe_tables.push_back(
      std::make_unique<Table>("../escape", std::move(unsafe_columns),
                              std::vector<TableConstraint>{}, std::vector<ForeignKeySpec>{},
                              std::vector<ForeignKey>{}));
  try {
    CsvWriter unsafe_writer(argv[1], unsafe_tables);
    std::cerr << "Expected unsafe table name to be rejected\n";
    return 1;
  } catch (const std::runtime_error&) {
  }

  return 0;
}
EOF

  if ! "${CXX:-c++}" -std=c++20 -I"${ROOT_DIR}/include" \
      "${source_file}" \
      "${ROOT_DIR}/src/output/CsvWriter.cpp" \
      "${ROOT_DIR}/src/schema/Column.cpp" \
      "${ROOT_DIR}/src/schema/Table.cpp" \
      -o "${binary_file}" >"${log_file}" 2>&1; then
    record_fail "${name}" "${log_file}"
    return
  fi

  if "${binary_file}" "${unsafe_directory}" >"${log_file}" 2>&1; then
    record_pass "${name}"
  else
    record_fail "${name}" "${log_file}"
  fi
}

run_postgres_copy_literal_formatting() {
  local name="unit/postgres_copy_literal_formatting"
  local source_file="${TMP_DIR}/postgres_copy_literal_formatting.cpp"
  local binary_file="${TMP_DIR}/postgres_copy_literal_formatting"
  local log_file="${TMP_DIR}/postgres_copy_literal_formatting.log"

  cat >"${source_file}" <<'EOF'
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "schemaforge/output/PostgresCopyWriter.h"
#include "schemaforge/schema/Column.h"
#include "schemaforge/schema/Table.h"

int main() {
  using namespace schemaforge;

  std::vector<ColumnPtr> columns;
  columns.push_back(std::make_unique<Column>("id", ColumnType{.data_type = DataType::INT}));
  columns.push_back(std::make_unique<Column>("odd\"column", ColumnType{.data_type = DataType::TEXT}));
  columns.push_back(std::make_unique<Column>("empty_text", ColumnType{.data_type = DataType::TEXT}));
  columns.push_back(std::make_unique<Column>("missing", ColumnType{.data_type = DataType::TEXT}));
  columns.push_back(std::make_unique<Column>("active", ColumnType{.data_type = DataType::BOOLEAN}));
  columns.push_back(std::make_unique<Column>("amount", ColumnType{.data_type = DataType::DECIMAL}));
  columns.push_back(std::make_unique<Column>("born_on", ColumnType{.data_type = DataType::DATE}));
  columns.push_back(std::make_unique<Column>("alarm", ColumnType{.data_type = DataType::TIME}));
  columns.push_back(
      std::make_unique<Column>("starts_at", ColumnType{.data_type = DataType::DATETIME}));

  std::vector<const Column*> row_columns;
  for (const auto& column : columns) {
    row_columns.push_back(column.get());
  }
  Table table("odd\"table", std::move(columns), {}, {}, {});
  GeneratedRow row{
      .table = &table,
      .columns = std::move(row_columns),
      .values = {
          GeneratedValue::integer(7),
          GeneratedValue::text(
              "slash\\ tab\t line\n carriage\r back\b form\f vertical\v null\\N end\\."),
          GeneratedValue::text(""), GeneratedValue::null(), GeneratedValue::boolean(true),
          GeneratedValue::numeric(12.5), GeneratedValue::date(DateValue{2026, 1, 1}),
          GeneratedValue::time(TimeValue{12, 30, 0}),
          GeneratedValue::date_time(
              DateTimeValue{DateValue{2026, 1, 1}, TimeValue{12, 30, 0}}),
      },
  };

  std::ostringstream output;
  PostgresCopyWriter writer(output);
  writer.begin_table(table);
  writer.write_row(row);
  writer.end_table(table);
  writer.close();

  const std::string expected =
      "BEGIN;\n"
      "COPY \"odd\"\"table\" (\"id\", \"odd\"\"column\", \"empty_text\", \"missing\", "
      "\"active\", \"amount\", \"born_on\", \"alarm\", \"starts_at\") FROM STDIN WITH "
      "(FORMAT text);\n"
      "7\tslash\\\\ tab\\t line\\n carriage\\r back\\b form\\f vertical\\v null\\\\N "
      "end\\\\.\t\t\\N\ttrue\t12.50\t2026-01-01\t12:30:00\t2026-01-01 12:30:00\n"
      "\\.\n"
      "COMMIT;\n";
  if (output.str() != expected) {
    std::cerr << "Expected:\n" << expected << "Actual:\n" << output.str();
    return 1;
  }
  return 0;
}
EOF

  if ! "${CXX:-c++}" -std=c++20 -I"${ROOT_DIR}/include" \
      "${source_file}" \
      "${ROOT_DIR}/src/output/PostgresCopyWriter.cpp" \
      "${ROOT_DIR}/src/schema/Column.cpp" \
      "${ROOT_DIR}/src/schema/Table.cpp" \
      -o "${binary_file}" >"${log_file}" 2>&1; then
    record_fail "${name}" "${log_file}"
    return
  fi

  if "${binary_file}" >"${log_file}" 2>&1; then
    record_pass "${name}"
  else
    record_fail "${name}" "${log_file}"
  fi
}

run_sql_literal_formatting() {
  local name="unit/sql_literal_formatting"
  local source_file="${TMP_DIR}/sql_literal_formatting.cpp"
  local binary_file="${TMP_DIR}/sql_literal_formatting"
  local log_file="${TMP_DIR}/sql_literal_formatting.log"

  cat >"${source_file}" <<'EOF'
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "schemaforge/generator/GeneratedValue.h"
#include "schemaforge/output/SqlInsertWriter.h"
#include "schemaforge/schema/Column.h"
#include "schemaforge/schema/Table.h"

int main() {
  using namespace schemaforge;

  std::vector<ColumnPtr> columns;
  columns.push_back(std::make_unique<Column>("id", ColumnType{.data_type = DataType::INT}));
  columns.push_back(
      std::make_unique<Column>("amount", ColumnType{.data_type = DataType::DECIMAL}));
  columns.push_back(std::make_unique<Column>("name", ColumnType{.data_type = DataType::TEXT}));
  columns.push_back(
      std::make_unique<Column>("active", ColumnType{.data_type = DataType::BOOLEAN}));
  columns.push_back(std::make_unique<Column>("born_on", ColumnType{.data_type = DataType::DATE}));
  columns.push_back(
      std::make_unique<Column>("alarm", ColumnType{.data_type = DataType::TIME}));
  columns.push_back(
      std::make_unique<Column>("starts_at", ColumnType{.data_type = DataType::DATETIME}));

  const Column* id = columns[0].get();
  const Column* amount = columns[1].get();
  const Column* name = columns[2].get();
  const Column* active = columns[3].get();
  const Column* born_on = columns[4].get();
  const Column* alarm = columns[5].get();
  const Column* starts_at = columns[6].get();
  Table table("samples", std::move(columns), {}, {}, {});

  TableData table_data{
      .table = &table,
      .columns = {
          ColumnData{.column = id, .data = {GeneratedValue::integer(7)}},
          ColumnData{.column = amount, .data = {GeneratedValue::numeric(12.5)}},
          ColumnData{.column = name, .data = {GeneratedValue::text("owner's sample")}},
          ColumnData{.column = active, .data = {GeneratedValue::boolean(true)}},
          ColumnData{.column = born_on,
                     .data = {GeneratedValue::date(DateValue{2026, 1, 1})}},
          ColumnData{.column = alarm,
                     .data = {GeneratedValue::time(TimeValue{12, 30, 0})}},
          ColumnData{.column = starts_at,
                     .data = {GeneratedValue::date_time(
                         DateTimeValue{DateValue{2026, 1, 1}, TimeValue{12, 30, 0}})}},
      },
  };

  const auto inserts = SqlInsertWriter::write_inserts({table_data});
  const std::string expected =
      "INSERT INTO samples (id, amount, name, active, born_on, alarm, starts_at) VALUES "
      "(7, 12.50, 'owner''s sample', true, '2026-01-01', '12:30:00', "
      "'2026-01-01 12:30:00');";

  if (inserts.size() != 1 || inserts.front() != expected) {
    std::cerr << "Expected: " << expected << '\n';
    if (!inserts.empty()) {
      std::cerr << "Actual:   " << inserts.front() << '\n';
    }
    return 1;
  }

  return 0;
}
EOF

  if ! "${CXX:-c++}" -std=c++20 -I"${ROOT_DIR}/include" \
      "${source_file}" \
      "${ROOT_DIR}/src/output/SqlInsertWriter.cpp" \
      "${ROOT_DIR}/src/schema/Column.cpp" \
      "${ROOT_DIR}/src/schema/Table.cpp" \
      -o "${binary_file}" >"${log_file}" 2>&1; then
    record_fail "${name}" "${log_file}"
    return
  fi

  if "${binary_file}" >"${log_file}" 2>&1; then
    record_pass "${name}"
  else
    record_fail "${name}" "${log_file}"
  fi
}

run_key_registry_pattern_sources() {
  local name="unit/key_registry_pattern_sources"
  local source_file="${TMP_DIR}/key_registry_pattern_sources.cpp"
  local binary_file="${TMP_DIR}/key_registry_pattern_sources"
  local log_file="${TMP_DIR}/key_registry_pattern_sources.log"

  cat >"${source_file}" <<'EOF'
#include <iostream>
#include <memory>
#include <string>
#include <type_traits>
#include <vector>

#include "schemaforge/generator/KeyRegistry.h"
#include "schemaforge/schema/Column.h"
#include "schemaforge/schema/Table.h"

std::string text_value(const schemaforge::GeneratedValue& value) {
  return value.visit([](const auto& typed_value) -> std::string {
    using ValueType = std::decay_t<decltype(typed_value)>;
    if constexpr (std::is_same_v<ValueType, std::string>) {
      return typed_value;
    }
    return "<not text>";
  });
}

int main() {
  using namespace schemaforge;

  std::vector<ColumnPtr> columns;
  columns.push_back(std::make_unique<Column>("email", ColumnType{.data_type = DataType::TEXT}));
  const Column* email = columns.front().get();

  std::vector<Column*> primary_columns{columns.front().get()};
  std::vector<TableConstraint> constraints{
      TableConstraint(ConstraintType::PrimaryKey, primary_columns)};

  Table table("users", std::move(columns), constraints, std::vector<ForeignKeySpec>{},
              std::vector<ForeignKey>{});

  KeyRegistry registry;
  registry.register_pattern(&table, primary_columns, KeyRegistry::PatternKeyKind::Email, "users",
                            0, 2);
  const auto key = registry.key_at_row(&table, primary_columns, 1).front();
  const std::string expected = "email_2@example.com";
  const std::string actual = text_value(key);

  if (actual != expected) {
    std::cerr << "Expected: " << expected << '\n';
    std::cerr << "Actual:   " << actual << '\n';
    return 1;
  }

  (void)email;
  return 0;
}
EOF

  if ! "${CXX:-c++}" -std=c++20 -I"${ROOT_DIR}/include" \
      "${source_file}" \
      "${ROOT_DIR}/src/generator/KeyRegistry.cpp" \
      "${ROOT_DIR}/src/generator/RealisticValueGenerator.cpp" \
      "${ROOT_DIR}/src/generator/TextGenerator.cpp" \
      "${ROOT_DIR}/src/domain/ColumnDomainResolver.cpp" \
      "${ROOT_DIR}/src/config/GenerationConfig.cpp" \
      "${ROOT_DIR}/src/schema/Column.cpp" \
      "${ROOT_DIR}/src/schema/Table.cpp" \
      -lyaml-cpp \
      -o "${binary_file}" >"${log_file}" 2>&1; then
    record_fail "${name}" "${log_file}"
    return
  fi

  if "${binary_file}" >"${log_file}" 2>&1; then
    record_pass "${name}"
  else
    record_fail "${name}" "${log_file}"
  fi
}

run_key_registry_composite_primary_key() {
  local name="unit/key_registry_composite_primary_key"
  local source_file="${TMP_DIR}/key_registry_composite_primary_key.cpp"
  local binary_file="${TMP_DIR}/key_registry_composite_primary_key"
  local log_file="${TMP_DIR}/key_registry_composite_primary_key.log"

  cat >"${source_file}" <<'EOF'
#include <cstdint>
#include <iostream>
#include <memory>
#include <string>
#include <type_traits>
#include <vector>

#include "schemaforge/config/GenerationConfig.h"
#include "schemaforge/generator/GeneratedValue.h"
#include "schemaforge/generator/KeyRegistry.h"
#include "schemaforge/generator/RandomEngine.h"
#include "schemaforge/schema/Column.h"
#include "schemaforge/schema/Table.h"

std::int64_t integer_value(const schemaforge::GeneratedValue& value) {
  return value.visit([](const auto& typed_value) -> std::int64_t {
    using ValueType = std::decay_t<decltype(typed_value)>;
    if constexpr (std::is_same_v<ValueType, std::int64_t>) {
      return typed_value;
    }
    return -1;
  });
}

int main() {
  using namespace schemaforge;

  std::vector<ColumnPtr> columns;
  columns.push_back(std::make_unique<Column>("user_id", ColumnType{.data_type = DataType::INT}));
  columns.push_back(std::make_unique<Column>("team_id", ColumnType{.data_type = DataType::INT}));
  std::vector<Column*> primary_columns{columns[0].get(), columns[1].get()};
  std::vector<TableConstraint> constraints{
      TableConstraint(ConstraintType::PrimaryKey, primary_columns)};

  auto table = std::make_unique<Table>("memberships", std::move(columns), constraints,
                                       std::vector<ForeignKeySpec>{}, std::vector<ForeignKey>{});
  std::vector<TablePtr> tables;
  tables.push_back(std::move(table));

  GenerationConfig config = GenerationConfig::make_default();
  config.table_row_counts["memberships"] = 4;

  const KeyRegistry registry = KeyRegistry::build_from_tables(tables, config);
  const auto key = registry.key_at_row(tables.front().get(), primary_columns, 3);

  if (key.size() != 2 || integer_value(key[0]) != 1 || integer_value(key[1]) != 4) {
    std::cerr << "Expected composite key row 4 to be (1, 4)\n";
    if (key.size() == 2) {
      std::cerr << "Actual: (" << integer_value(key[0]) << ", " << integer_value(key[1])
                << ")\n";
    }
    return 1;
  }

  RandomEngine random_engine(42);
  const auto random_key = registry.random_key(tables.front().get(), primary_columns, random_engine);
  if (random_key.size() != 2) {
    std::cerr << "Expected random composite key to return 2 values\n";
    return 1;
  }

  return 0;
}
EOF

  if ! "${CXX:-c++}" -std=c++20 -I"${ROOT_DIR}/include" \
      "${source_file}" \
      "${ROOT_DIR}/src/generator/KeyRegistry.cpp" \
      "${ROOT_DIR}/src/generator/RealisticValueGenerator.cpp" \
      "${ROOT_DIR}/src/generator/TextGenerator.cpp" \
      "${ROOT_DIR}/src/domain/ColumnDomainResolver.cpp" \
      "${ROOT_DIR}/src/config/GenerationConfig.cpp" \
      "${ROOT_DIR}/src/schema/Column.cpp" \
      "${ROOT_DIR}/src/schema/Table.cpp" \
      -lyaml-cpp \
      -o "${binary_file}" >"${log_file}" 2>&1; then
    record_fail "${name}" "${log_file}"
    return
  fi

  if "${binary_file}" >"${log_file}" 2>&1; then
    record_pass "${name}"
  else
    record_fail "${name}" "${log_file}"
  fi
}

run_key_registry_composite_unique() {
  local name="unit/key_registry_composite_unique"
  local source_file="${TMP_DIR}/key_registry_composite_unique.cpp"
  local binary_file="${TMP_DIR}/key_registry_composite_unique"
  local log_file="${TMP_DIR}/key_registry_composite_unique.log"

  cat >"${source_file}" <<'EOF'
#include <cstdint>
#include <iostream>
#include <memory>
#include <string>
#include <type_traits>
#include <vector>

#include "schemaforge/config/GenerationConfig.h"
#include "schemaforge/generator/GeneratedValue.h"
#include "schemaforge/generator/KeyRegistry.h"
#include "schemaforge/generator/RandomEngine.h"
#include "schemaforge/schema/Column.h"
#include "schemaforge/schema/Table.h"

std::int64_t integer_value(const schemaforge::GeneratedValue& value) {
  return value.visit([](const auto& typed_value) -> std::int64_t {
    using ValueType = std::decay_t<decltype(typed_value)>;
    if constexpr (std::is_same_v<ValueType, std::int64_t>) {
      return typed_value;
    }
    return -1;
  });
}

bool is_expected_tuple(const std::vector<schemaforge::GeneratedValue>& key) {
  if (key.size() != 2) {
    return false;
  }
  const auto left = integer_value(key[0]);
  const auto right = integer_value(key[1]);
  return left == 1 && right >= 1 && right <= 4;
}

int main() {
  using namespace schemaforge;

  std::vector<ColumnPtr> columns;
  columns.push_back(std::make_unique<Column>("user_id", ColumnType{.data_type = DataType::INT}));
  columns.push_back(std::make_unique<Column>("product_id", ColumnType{.data_type = DataType::INT}));
  std::vector<Column*> unique_columns{columns[0].get(), columns[1].get()};
  std::vector<TableConstraint> constraints{
      TableConstraint(ConstraintType::Unique, unique_columns)};

  auto table = std::make_unique<Table>("cart_items", std::move(columns), constraints,
                                       std::vector<ForeignKeySpec>{}, std::vector<ForeignKey>{});
  std::vector<TablePtr> tables;
  tables.push_back(std::move(table));

  GenerationConfig config = GenerationConfig::make_default();
  config.table_row_counts["cart_items"] = 4;

  const KeyRegistry registry = KeyRegistry::build_from_tables(tables, config);
  const auto key = registry.key_at_row(tables.front().get(), unique_columns, 1);
  if (key.size() != 2 || integer_value(key[0]) != 1 || integer_value(key[1]) != 2) {
    std::cerr << "Expected composite UNIQUE row 2 to be (1, 2)\n";
    if (key.size() == 2) {
      std::cerr << "Actual: (" << integer_value(key[0]) << ", " << integer_value(key[1])
                << ")\n";
    }
    return 1;
  }

  RandomEngine random_engine(42);
  const auto random_key = registry.random_key(tables.front().get(), unique_columns, random_engine);
  if (!is_expected_tuple(random_key)) {
    std::cerr << "Expected random composite UNIQUE key to return one registered tuple\n";
    return 1;
  }

  return 0;
}
EOF

  if ! "${CXX:-c++}" -std=c++20 -I"${ROOT_DIR}/include" \
      "${source_file}" \
      "${ROOT_DIR}/src/generator/KeyRegistry.cpp" \
      "${ROOT_DIR}/src/generator/RealisticValueGenerator.cpp" \
      "${ROOT_DIR}/src/generator/TextGenerator.cpp" \
      "${ROOT_DIR}/src/domain/ColumnDomainResolver.cpp" \
      "${ROOT_DIR}/src/config/GenerationConfig.cpp" \
      "${ROOT_DIR}/src/schema/Column.cpp" \
      "${ROOT_DIR}/src/schema/Table.cpp" \
      -lyaml-cpp \
      -o "${binary_file}" >"${log_file}" 2>&1; then
    record_fail "${name}" "${log_file}"
    return
  fi

  if "${binary_file}" >"${log_file}" 2>&1; then
    record_pass "${name}"
  else
    record_fail "${name}" "${log_file}"
  fi
}

require_artifact_contains() {
  local name="$1"
  local file="$2"
  local expected_text="$3"
  local log_file="${TMP_DIR}/${name//\//_}_artifact.log"

  if [[ -f "${file}" ]] && grep -Fq "${expected_text}" "${file}"; then
    record_pass "${name}"
    return
  fi

  {
    echo "Expected artifact to contain:"
    echo "${expected_text}"
    echo
    echo "Artifact: ${file}"
    if [[ -f "${file}" ]]; then
      echo "----- artifact -----"
      cat "${file}"
    else
      echo "Artifact does not exist."
    fi
  } >"${log_file}"
  record_fail "${name}" "${log_file}"
}

run_realistic_config() {
  local name="valid/realistic_config"
  local config_file="${TMP_DIR}/realistic_config.yaml"
  local first_output="${ARTIFACT_DIR}/valid_realistic_config_a.sql"
  local second_output="${ARTIFACT_DIR}/valid_realistic_config_b.sql"
  local third_output="${ARTIFACT_DIR}/valid_realistic_config_seed_43.sql"
  local first_benchmark="${ARTIFACT_DIR}/valid_realistic_config_a_benchmark.yaml"
  local second_benchmark="${ARTIFACT_DIR}/valid_realistic_config_b_benchmark.yaml"
  local third_benchmark="${ARTIFACT_DIR}/valid_realistic_config_seed_43_benchmark.yaml"
  local log_file="${TMP_DIR}/realistic_config.log"

  if ! "${BINARY}" init --schema "${ROOT_DIR}/tests/valid/realistic_config/schema.sql" \
      --config "${config_file}" >"${log_file}" 2>&1; then
    record_fail "${name}" "${log_file}"
    return
  fi
  perl -0pi -e 's/(realistic: )false/${1}true/' "${config_file}"
  perl -0pi -e 's/(users:\n\s+rows: )10/${1}6\n    columns:\n      age:\n        min: 25\n        max: 30\n      status:\n        values: [active, inactive]\n      middle_name:\n        null_probability: 1.0/' "${config_file}"
  perl -0pi -e 's/(messages:\n\s+rows: )10/${1}6/' "${config_file}"
  perl -0pi -e "s#file: output\\.sql#file: ${first_output}#; s#file: benchmark\\.yaml#file: ${first_benchmark}#" "${config_file}"

  if ! "${BINARY}" generate --config "${config_file}" >"${log_file}" 2>&1; then
    record_fail "${name}" "${log_file}"
    return
  fi
  perl -0pi -e "s#file: ${first_output}#file: ${second_output}#; s#file: ${first_benchmark}#file: ${second_benchmark}#" "${config_file}"

  if ! "${BINARY}" generate --config "${config_file}" >>"${log_file}" 2>&1; then
    record_fail "${name}" "${log_file}"
    return
  fi
  perl -0pi -e "s#file: ${second_output}#file: ${third_output}#; s#file: ${second_benchmark}#file: ${third_benchmark}#; s/(seed: )42/\${1}43/" "${config_file}"

  if "${BINARY}" generate --config "${config_file}" >>"${log_file}" 2>&1 &&
      cmp -s "${first_output}" "${second_output}" &&
      ! cmp -s "${first_output}" "${third_output}" &&
      grep -Eq "[a-z]+\\.[a-z]+\\+[0-9]+@" "${first_output}" &&
      grep -q ", 25, 'active', NULL," "${first_output}" &&
      grep -q "'Phoenix', 'AZ', '85001'" "${first_output}" &&
      grep -q "SQLite Validation Result: Valid" "${log_file}" &&
      ! grep -q "first_name_1" "${first_output}"; then
    record_pass "${name}"
  else
    record_fail "${name}" "${log_file}"
  fi
}

run_heuristic_columns() {
  local name="valid/heuristic_columns"
  local config_file="${TMP_DIR}/heuristic_columns.yaml"
  local first_output="${ARTIFACT_DIR}/valid_heuristic_columns_a.sql"
  local second_output="${ARTIFACT_DIR}/valid_heuristic_columns_b.sql"
  local first_benchmark="${ARTIFACT_DIR}/valid_heuristic_columns_a_benchmark.yaml"
  local second_benchmark="${ARTIFACT_DIR}/valid_heuristic_columns_b_benchmark.yaml"
  local log_file="${TMP_DIR}/heuristic_columns.log"

  if ! "${BINARY}" init --schema "${ROOT_DIR}/tests/valid/heuristic_columns/schema.sql" \
      --config "${config_file}" >"${log_file}" 2>&1; then
    record_fail "${name}" "${log_file}"
    return
  fi
  perl -0pi -e 's/(users:\n\s+rows: )10/${1}3\n    columns:\n      optional_phone:\n        null_probability: 1.0/; s/(messages:\n\s+rows: )10/${1}3/; s/(typed_precedence:\n\s+rows: )10/${1}3/' "${config_file}"
  perl -0pi -e "s#file: output\\.sql#file: ${first_output}#; s#file: benchmark\\.yaml#file: ${first_benchmark}#" "${config_file}"

  if ! "${BINARY}" generate --config "${config_file}" >"${log_file}" 2>&1; then
    record_fail "${name}" "${log_file}"
    return
  fi
  perl -0pi -e "s#file: ${first_output}#file: ${second_output}#; s#file: ${first_benchmark}#file: ${second_benchmark}#" "${config_file}"

  if "${BINARY}" generate --config "${config_file}" >>"${log_file}" 2>&1 &&
      cmp -s "${first_output}" "${second_output}" &&
      grep -q "VALUES (1, 'email_1@example.com', 'first_name_1', 'last_name_1', 'user_1', '555-000-0001', 10.50, 10.50, 'active', 'queued', '2026-01-01 00:00:00', true, NULL);" "${first_output}" &&
      grep -q "VALUES (2, 'email_2@example.com', 'first_name_2', 'last_name_2', 'user_2', '555-000-0002', 20.50, 20.50, 'inactive', 'done', '2026-01-01 00:00:01', false, NULL);" "${first_output}" &&
      grep -q "INSERT INTO typed_precedence (id, email) VALUES (1, 1);" "${first_output}" &&
      grep -q "SQLite Validation Result: Valid" "${log_file}"; then
    record_pass "${name}"
  else
    record_fail "${name}" "${log_file}"
  fi
}

run_configured_parent_key() {
  local name="valid/configured_parent_key"
  local config_file="${TMP_DIR}/configured_parent_key.yaml"
  local output_file="${ARTIFACT_DIR}/valid_configured_parent_key.sql"
  local benchmark_file="${ARTIFACT_DIR}/valid_configured_parent_key_benchmark.yaml"
  local log_file="${TMP_DIR}/configured_parent_key.log"

  "${BINARY}" init --schema "${ROOT_DIR}/tests/valid/realistic_config/schema.sql" \
    --config "${config_file}" >"${log_file}" 2>&1 || {
    record_fail "${name}" "${log_file}"; return;
  }
  perl -0pi -e 's/(users:\n\s+rows: )10/${1}3\n    columns:\n      email:\n        values: ["001", "002", "003"]/; s/(messages:\n\s+rows: )10/${1}3/' "${config_file}"
  perl -0pi -e "s#file: output\\.sql#file: ${output_file}#; s#file: benchmark\\.yaml#file: ${benchmark_file}#" "${config_file}"

  if "${BINARY}" generate --config "${config_file}" >"${log_file}" 2>&1 &&
      grep -q "'001'" "${output_file}" &&
      grep -q "'002'" "${output_file}" &&
      grep -q "'003'" "${output_file}" &&
      grep -q "SQLite Validation Result: Valid" "${log_file}"; then
    record_pass "${name}"
  else
    record_fail "${name}" "${log_file}"
  fi
}

run_nullable_composite_fk() {
  local name="valid/nullable_composite_fk"
  local config_file="${TMP_DIR}/nullable_composite_fk.yaml"
  local output_file="${ARTIFACT_DIR}/valid_nullable_composite_fk.sql"
  local benchmark_file="${ARTIFACT_DIR}/valid_nullable_composite_fk_benchmark.yaml"
  local log_file="${TMP_DIR}/nullable_composite_fk.log"

  "${BINARY}" init --schema "${ROOT_DIR}/tests/valid/composite_fk_primary_key/schema.sql" \
    --config "${config_file}" >"${log_file}" 2>&1 || {
    record_fail "${name}" "${log_file}"; return;
  }
  perl -0pi -e 's/(memberships:\n\s+rows: )10/${1}4/; s/(membership_events:\n\s+rows: )10/${1}4\n    columns:\n      user_id:\n        null_probability: 1.0/' "${config_file}"
  perl -0pi -e "s#file: output\\.sql#file: ${output_file}#; s#file: benchmark\\.yaml#file: ${benchmark_file}#" "${config_file}"

  if "${BINARY}" generate --config "${config_file}" >"${log_file}" 2>&1 &&
      [[ "$(grep -c 'membership_events.*NULL, NULL' "${output_file}")" -eq 4 ]] &&
      grep -q "SQLite Validation Result: Valid" "${log_file}"; then
    record_pass "${name}"
  else
    record_fail "${name}" "${log_file}"
  fi
}

run_invalid_column_config() {
  local name="invalid/column_config"
  local config_file="${TMP_DIR}/invalid_column_config.yaml"
  local log_file="${TMP_DIR}/invalid_column_config.log"

  "${BINARY}" init --schema "${ROOT_DIR}/tests/valid/realistic_config/schema.sql" \
    --config "${config_file}" >"${log_file}" 2>&1 || {
    record_fail "${name}" "${log_file}"; return;
  }
  perl -0pi -e 's/(users:\n\s+rows: 10)/${1}\n    columns:\n      missing:\n        values: [x]\n      age:\n        min: 90\n      id:\n        null_probability: 0.5/' "${config_file}"

  if ! "${BINARY}" generate --config "${config_file}" >"${log_file}" 2>&1 &&
      grep -q "unknown column 'users.missing'" "${log_file}" &&
      grep -q "does not overlap SQL CHECK for column 'users.age'" "${log_file}" &&
      grep -q "null_probability is not allowed for required column 'users.id'" "${log_file}"; then
    record_pass "${name}"
  else
    record_fail "${name}" "${log_file}"
  fi
}

cd "${ROOT_DIR}" || exit 1
build_project
prepare_artifacts

run_usage_invalid
run_valid "valid/basic_fk" "tests/valid/basic_fk/schema.sql"
run_valid "valid/reverse_table_order" "tests/valid/reverse_table_order/schema.sql"
run_valid "valid/single_table_random_values" "tests/valid/single_table_random_values/schema.sql"
run_valid "valid/unique_text" "tests/valid/unique_text/schema.sql"
run_valid "valid/unique_fk_within_parent_capacity" "tests/valid/unique_fk_within_parent_capacity/schema.sql"
run_valid "valid/unique_boolean_within_capacity" "tests/valid/unique_boolean_within_capacity/schema.sql" --rows flags=2
run_valid "valid/dependency_chain" "tests/valid/dependency_chain/schema.sql"
run_valid "valid/complex_marketplace" "tests/valid/complex_marketplace/schema.sql"
run_valid "valid/all_supported_types" "tests/valid/all_supported_types/schema.sql"
run_valid "valid/char_primary_key" "tests/valid/char_primary_key/schema.sql"
run_valid "valid/char_unique_within_capacity" "tests/valid/char_unique_within_capacity/schema.sql" --rows flags=26
run_valid "valid/char_cycle" "tests/valid/char_cycle/schema.sql" --rows flags=28
run_valid "valid/text_primary_key_email" "tests/valid/text_primary_key_email/schema.sql"
run_valid "valid/text_primary_key_table_key" "tests/valid/text_primary_key_table_key/schema.sql"
run_valid "valid/varchar_primary_key" "tests/valid/varchar_primary_key/schema.sql"
run_valid "valid/text_fk_email" "tests/valid/text_fk_email/schema.sql"
run_valid "valid/text_fk_table_key" "tests/valid/text_fk_table_key/schema.sql"
run_valid "valid/varchar_fk" "tests/valid/varchar_fk/schema.sql"
run_valid "valid/unique_text_fk_within_capacity" "tests/valid/unique_text_fk_within_capacity/schema.sql"
run_valid "valid/check_greater_equal" "tests/valid/check_greater_equal/schema.sql"
run_valid "valid/check_less_equal" "tests/valid/check_less_equal/schema.sql"
run_valid "valid/check_greater_than" "tests/valid/check_greater_than/schema.sql"
run_valid "valid/check_less_than" "tests/valid/check_less_than/schema.sql"
run_valid "valid/check_between" "tests/valid/check_between/schema.sql"
run_valid "valid/check_text_in" "tests/valid/check_text_in/schema.sql"
run_valid "valid/check_numeric_in" "tests/valid/check_numeric_in/schema.sql"
run_valid "valid/check_decimal_min" "tests/valid/check_decimal_min/schema.sql"
run_valid "valid/check_decimal_greater_than" "tests/valid/check_decimal_greater_than/schema.sql"
run_valid "valid/check_decimal_greater_equal" "tests/valid/check_decimal_greater_equal/schema.sql"
run_valid "valid/check_decimal_less_than" "tests/valid/check_decimal_less_than/schema.sql"
run_valid "valid/check_decimal_less_equal" "tests/valid/check_decimal_less_equal/schema.sql"
run_valid "valid/unique_decimal_check" "tests/valid/unique_decimal_check/schema.sql"
run_valid "valid/composite_primary_key" "tests/valid/composite_primary_key/schema.sql" --rows memberships=4
run_valid "valid/composite_primary_key_text" "tests/valid/composite_primary_key_text/schema.sql"
run_valid "valid/composite_unique_int" "tests/valid/composite_unique_int/schema.sql"
run_valid "valid/composite_unique_text" "tests/valid/composite_unique_text/schema.sql"
run_valid "valid/composite_unique_fk" "tests/valid/composite_unique_fk/schema.sql"
run_valid "valid/composite_fk_primary_key" "tests/valid/composite_fk_primary_key/schema.sql" --rows memberships=4 --rows membership_events=4
run_valid "valid/composite_fk_unique" "tests/valid/composite_fk_unique/schema.sql" --rows regions=4 --rows offices=4
run_deterministic "valid/deterministic_output" "tests/valid/basic_fk/schema.sql" --seed 42
run_heuristic_columns
run_realistic_config
run_configured_parent_key
run_nullable_composite_fk
run_sqlite_disabled
run_benchmark_example_smoke "single_table_1m" "skipped" 10
run_benchmark_example_smoke "users_orders_1m" "passed" 20
run_benchmark_example_smoke "ecommerce_large" "skipped" 40
run_benchmark_unknown_case
run_csv_single_fk
run_csv_composite_fk
run_csv_zero_rows
run_csv_missing_directory
run_csv_literal_formatting
run_postgres_copy_single_fk
run_postgres_copy_composite_fk
run_postgres_copy_zero_rows
run_postgres_docker_unavailable
run_postgres_validation_requires_supported_output
run_postgres_copy_literal_formatting
run_sql_literal_formatting
run_key_registry_pattern_sources
run_key_registry_composite_primary_key
run_key_registry_composite_unique
require_artifact_contains "valid/all_supported_types_output" "${ARTIFACT_DIR}/valid_all_supported_types.sql" \
  "VALUES (1, 1, 1, 'title_1', 'slug_1', 'A', 'AA', 10.50, 10.50, 10.50, 10.50, true, '2026-01-01', '2026-01-01 00:00:00', '00:00:01');"
require_artifact_contains "valid/char_primary_key_output" "${ARTIFACT_DIR}/valid_char_primary_key.sql" \
  "VALUES ('AA', 'name_1');"
require_artifact_contains "valid/char_cycle_output" "${ARTIFACT_DIR}/valid_char_cycle.sql" \
  "VALUES (27, 'A');"
require_artifact_contains "valid/text_primary_key_email_output" "${ARTIFACT_DIR}/valid_text_primary_key_email.sql" \
  "VALUES ('email_1@example.com', 'name_1');"
require_artifact_contains "valid/text_primary_key_table_key_output" "${ARTIFACT_DIR}/valid_text_primary_key_table_key.sql" \
  "VALUES ('users_key_1', 'name_1');"
require_artifact_contains "valid/varchar_primary_key_output" "${ARTIFACT_DIR}/valid_varchar_primary_key.sql" \
  "VALUES ('products_key_1', 'name_1');"
require_artifact_contains "valid/text_fk_email_output" "${ARTIFACT_DIR}/valid_text_fk_email.sql" \
  "VALUES (1, 'email_8@example.com');"
require_artifact_contains "valid/text_fk_table_key_output" "${ARTIFACT_DIR}/valid_text_fk_table_key.sql" \
  "VALUES (1, 'users_key_8');"
require_artifact_contains "valid/varchar_fk_output" "${ARTIFACT_DIR}/valid_varchar_fk.sql" \
  "VALUES (1, 'products_key_8');"
require_artifact_contains "valid/unique_text_fk_within_capacity_output" "${ARTIFACT_DIR}/valid_unique_text_fk_within_capacity.sql" \
  "VALUES (1, 'email_1@example.com');"
require_artifact_contains "valid/check_greater_equal_output" "${ARTIFACT_DIR}/valid_check_greater_equal.sql" \
  "VALUES (1, 18);"
require_artifact_contains "valid/check_less_equal_output" "${ARTIFACT_DIR}/valid_check_less_equal.sql" \
  "VALUES (1, 1);"
require_artifact_contains "valid/check_greater_than_output" "${ARTIFACT_DIR}/valid_check_greater_than.sql" \
  "VALUES (1, 1);"
require_artifact_contains "valid/check_less_than_output" "${ARTIFACT_DIR}/valid_check_less_than.sql" \
  "VALUES (1, 1);"
require_artifact_contains "valid/check_between_output" "${ARTIFACT_DIR}/valid_check_between.sql" \
  "VALUES (1, 18);"
require_artifact_contains "valid/check_text_in_output" "${ARTIFACT_DIR}/valid_check_text_in.sql" \
  "VALUES (1, 'pending');"
require_artifact_contains "valid/check_text_in_cycle_output" "${ARTIFACT_DIR}/valid_check_text_in.sql" \
  "VALUES (3, 'cancelled');"
require_artifact_contains "valid/check_numeric_in_output" "${ARTIFACT_DIR}/valid_check_numeric_in.sql" \
  "VALUES (1, 10);"
require_artifact_contains "valid/check_decimal_min_output" "${ARTIFACT_DIR}/valid_check_decimal_min.sql" \
  "VALUES (1, 0.00);"
require_artifact_contains "valid/check_decimal_greater_than_output" "${ARTIFACT_DIR}/valid_check_decimal_greater_than.sql" \
  "VALUES (1, 0.51);"
require_artifact_contains "valid/check_decimal_greater_equal_output" "${ARTIFACT_DIR}/valid_check_decimal_greater_equal.sql" \
  "VALUES (1, 0.50);"
require_artifact_contains "valid/check_decimal_less_than_output" "${ARTIFACT_DIR}/valid_check_decimal_less_than.sql" \
  "VALUES (2, 0.01);"
require_artifact_contains "valid/check_decimal_less_equal_output" "${ARTIFACT_DIR}/valid_check_decimal_less_equal.sql" \
  "VALUES (2, 10.50);"
require_artifact_contains "valid/unique_decimal_check_output" "${ARTIFACT_DIR}/valid_unique_decimal_check.sql" \
  "VALUES (1, 0.51);"
require_artifact_contains "valid/composite_primary_key_output_first" "${ARTIFACT_DIR}/valid_composite_primary_key.sql" \
  "VALUES (1, 1);"
require_artifact_contains "valid/composite_primary_key_output_nested" "${ARTIFACT_DIR}/valid_composite_primary_key.sql" \
  "VALUES (2, 1);"
require_artifact_contains "valid/composite_primary_key_text_output" "${ARTIFACT_DIR}/valid_composite_primary_key_text.sql" \
  "VALUES ('namespace_1', 'slug_2');"
require_artifact_contains "valid/composite_unique_int_output_first" "${ARTIFACT_DIR}/valid_composite_unique_int.sql" \
  "VALUES (1, 1, 1);"
require_artifact_contains "valid/composite_unique_int_output_nested" "${ARTIFACT_DIR}/valid_composite_unique_int.sql" \
  "VALUES (2, 1, 2);"
require_artifact_contains "valid/composite_unique_text_output" "${ARTIFACT_DIR}/valid_composite_unique_text.sql" \
  "VALUES (2, 'user_key_1', 'product_key_2');"
require_artifact_contains "valid/composite_unique_fk_output_first" "${ARTIFACT_DIR}/valid_composite_unique_fk.sql" \
  "VALUES (1, 1, 1);"
require_artifact_contains "valid/composite_unique_fk_output_nested" "${ARTIFACT_DIR}/valid_composite_unique_fk.sql" \
  "VALUES (2, 1, 2);"
require_artifact_contains "valid/composite_fk_primary_key_output" "${ARTIFACT_DIR}/valid_composite_fk_primary_key.sql" \
  "INSERT INTO membership_events (id, user_id, team_id) VALUES (2, 2, 1);"
require_artifact_contains "valid/composite_fk_unique_output" "${ARTIFACT_DIR}/valid_composite_fk_unique.sql" \
  "INSERT INTO offices (id, country_code, region_code) VALUES (2, 2, 1);"

run_invalid "invalid/missing_fk_table" "tests/invalid/missing_fk_table/schema.sql" "Referenced table 'users' not found"
run_invalid "invalid/missing_fk_column" "tests/invalid/missing_fk_column/schema.sql" "Referenced column 'user_id' not found"
run_invalid "invalid/duplicate_table_names" "tests/invalid/duplicate_table_names/schema.sql" "Duplicate table name 'users'"
run_invalid "invalid/duplicate_column_names" "tests/invalid/duplicate_column_names/schema.sql" "Duplicate column name 'users.id'"
run_invalid "invalid/missing_pk_column" "tests/invalid/missing_pk_column/schema.sql" "Primary key on table 'users' references an unknown column"
run_invalid "invalid/missing_unique_column" "tests/invalid/missing_unique_column/schema.sql" "Unique constraint on table 'users' references an unknown column"
run_invalid "invalid/missing_fk_local_column" "tests/invalid/missing_fk_local_column/schema.sql" "unknown local column 'user_id'"
run_invalid "invalid/fk_column_count_mismatch" "tests/invalid/fk_column_count_mismatch/schema.sql" "has 2 local columns but 1 referenced columns"
run_invalid "invalid/fk_type_mismatch" "tests/invalid/fk_type_mismatch/schema.sql" "type does not match"
run_invalid "invalid/fk_references_non_unique" "tests/invalid/fk_references_non_unique/schema.sql" "must reference a PRIMARY KEY or UNIQUE constraint"
run_invalid "invalid/cycle" "tests/invalid/cycle/schema.sql" "Cycle detected"
run_invalid "invalid/self_reference" "tests/invalid/self_reference/schema.sql" "Self-referencing foreign key"
run_invalid "invalid/unsupported_pk_generation_type" "tests/invalid/unsupported_pk_generation_type/schema.sql" "Primary key column 'users.born_on' must use INT, BIGINT, SMALLINT, TEXT, VARCHAR, or CHAR for generation"
run_invalid "invalid/unsupported_fk_generation_type" "tests/invalid/unsupported_fk_generation_type/schema.sql" "Foreign key column 'orders.user_born_on' must use INT, BIGINT, SMALLINT, TEXT, or VARCHAR for generation"
run_valid "valid/date_generation" "tests/invalid/unsupported_date/schema.sql"
run_invalid "invalid/unique_boolean_too_many" "tests/invalid/unique_boolean_too_many/schema.sql" "UNIQUE BOOLEAN"
run_invalid "invalid/unique_char_too_many" "tests/invalid/unique_char_too_many/schema.sql" "Column users.code is UNIQUE CHAR(1) and can only produce 26 distinct values." --rows users=30
run_invalid "invalid/unsupported_json" "tests/invalid/unsupported_json/schema.sql" "Unsupported generation type JSON for column users.metadata"
run_invalid "invalid/unsupported_char_fk" "tests/invalid/unsupported_char_fk/schema.sql" "Foreign key column 'users.country_code' must use INT, BIGINT, SMALLINT, TEXT, or VARCHAR for generation"
run_invalid "invalid/unique_fk_too_many_children" "tests/invalid/unique_fk_too_many_children/schema.sql" "UNIQUE foreign key" --rows orders=25
run_invalid "invalid/unique_text_fk_too_many_children" "tests/invalid/unique_text_fk_too_many_children/schema.sql" "UNIQUE foreign key" --rows orders=25
run_invalid "invalid/zero_parent_rows_for_fk" "tests/invalid/zero_parent_rows_for_fk/schema.sql" "references table 'users', but that table has 0 rows" --rows users=0 --rows orders=5
run_invalid "invalid/unique_check_range_too_many" "tests/invalid/unique_check_range_too_many/schema.sql" "Column users.age is UNIQUE CHECK and can only produce 13 distinct values." --rows users=14
run_invalid "invalid/unique_check_in_too_many" "tests/invalid/unique_check_in_too_many/schema.sql" "Column users.status is UNIQUE CHECK and can only produce 2 distinct values." --rows users=3
run_invalid "invalid/unsupported_check" "tests/invalid/unsupported_check/schema.sql" "Unsupported CHECK constraint on users.age: CHECK (age + score > 100)"
run_invalid "invalid/composite_unique_fk_too_many" "tests/invalid/composite_unique_fk_too_many/schema.sql" "Composite UNIQUE(user_id, product_id) on table 'order_items' can only produce 200 distinct tuples." --rows users=10 --rows products=20 --rows order_items=250
run_invalid "invalid/composite_unique_check_too_many" "tests/invalid/composite_unique_check_too_many/schema.sql" "Composite UNIQUE(age, status) on table 'users' can only produce 4 distinct tuples." --rows users=5
run_invalid "invalid/composite_primary_key_too_many" "tests/invalid/composite_primary_key_too_many/schema.sql" "Composite PRIMARY KEY(user_id, team_id) on table 'memberships' can only produce 4 distinct tuples." --rows memberships=5
run_invalid "invalid/composite_unique_missing_column" "tests/invalid/composite_unique_missing_column/schema.sql" "Unique constraint on table 'cart_items' references an unknown column"
run_invalid "invalid/multiple_errors" "tests/invalid/multiple_errors/schema.sql" "Duplicate column name 'users.id'"
run_config_unknown_table
run_missing_schema_path
run_missing_schema_file
run_invalid_column_config

echo
echo "Passed: ${PASSED}"
echo "Failed: ${FAILED}"

if [[ "${FAILED}" -ne 0 ]]; then
  exit 1
fi
