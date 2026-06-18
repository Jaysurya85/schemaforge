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
run_valid "valid/composite_unique_int" "tests/valid/composite_unique_int/schema.sql"
run_valid "valid/composite_unique_text" "tests/valid/composite_unique_text/schema.sql"
run_valid "valid/composite_unique_fk" "tests/valid/composite_unique_fk/schema.sql"
run_deterministic "valid/deterministic_output" "tests/valid/basic_fk/schema.sql" --seed 42
run_sqlite_disabled
run_sql_literal_formatting
run_key_registry_pattern_sources
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
run_invalid "invalid/composite_primary_key" "tests/invalid/composite_primary_key/schema.sql" "Composite primary keys are not supported yet"
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
run_invalid "invalid/composite_unique_missing_column" "tests/invalid/composite_unique_missing_column/schema.sql" "Unique constraint on table 'cart_items' references an unknown column"
run_invalid "invalid/multiple_errors" "tests/invalid/multiple_errors/schema.sql" "Duplicate column name 'users.id'"
run_config_unknown_table
run_missing_schema_path
run_missing_schema_file

echo
echo "Passed: ${PASSED}"
echo "Failed: ${FAILED}"

if [[ "${FAILED}" -ne 0 ]]; then
  exit 1
fi
