#include "schemaforge/generator/GenerationPlan.h"

#include <algorithm>
#include <cmath>
#include <cctype>
#include <sstream>
#include <stdexcept>
#include <type_traits>
#include <typeinfo>
#include <unordered_set>
#include <variant>

#include "schemaforge/domain/ColumnDomainResolver.h"
#include "schemaforge/generator/RealisticValueGenerator.h"
#include "schemaforge/generator/ValueGenerator.h"

namespace schemaforge {

namespace {

const ForeignKey* find_single_column_foreign_key(const Table& table, const Column* column) {
  for (const auto& foreign_key : table.get_foreign_keys()) {
    if (foreign_key.local_columns.size() == 1 && foreign_key.referenced_columns.size() == 1 &&
        foreign_key.local_columns.front() == column) {
      return &foreign_key;
    }
  }
  return nullptr;
}

const ForeignKey* find_foreign_key(const Table& table, const Column* column) {
  for (const auto& foreign_key : table.get_foreign_keys()) {
    if (std::ranges::find(foreign_key.local_columns, column) != foreign_key.local_columns.end()) {
      return &foreign_key;
    }
  }
  return nullptr;
}

bool is_composite_foreign_key(const ForeignKey& foreign_key) {
  return foreign_key.local_columns.size() > 1 && foreign_key.referenced_columns.size() > 1;
}

bool has_single_column_constraint(const Table& table, const Column* column,
                                  ConstraintType constraint_type) {
  for (const auto& constraint : table.get_table_constraints()) {
    if (constraint.type == constraint_type && constraint.columns.size() == 1 &&
        constraint.columns.front() == column) {
      return true;
    }
  }
  return false;
}

std::vector<Column*> key_columns_for_single_constraint(const Table& table, const Column* column,
                                                       ConstraintType constraint_type) {
  for (const auto& constraint : table.get_table_constraints()) {
    if (constraint.type == constraint_type && constraint.columns.size() == 1 &&
        constraint.columns.front() == column) {
      return constraint.columns;
    }
  }
  return {};
}

bool has_already_unique_member(const Table& table, const TableConstraint& constraint) {
  return std::ranges::any_of(constraint.columns, [&table](const Column* column) {
    return has_single_column_constraint(table, column, ConstraintType::PrimaryKey) ||
           has_single_column_constraint(table, column, ConstraintType::Unique);
  });
}

bool is_composite_key_constraint(const TableConstraint& constraint) {
  return (constraint.type == ConstraintType::PrimaryKey || constraint.type == ConstraintType::Unique) &&
         constraint.columns.size() > 1;
}

const ColumnData* find_column_data(const std::vector<ColumnData>& columns, const Column* column) {
  for (const auto& column_data : columns) {
    if (column_data.column == column) {
      return &column_data;
    }
  }
  return nullptr;
}

ColumnData* find_column_data(std::vector<ColumnData>& columns, const Column* column) {
  for (auto& column_data : columns) {
    if (column_data.column == column) {
      return &column_data;
    }
  }
  return nullptr;
}

std::string value_key(const GeneratedValue& value) {
  std::ostringstream output;
  value.visit([&output](const auto& typed_value) {
    using ValueType = std::decay_t<decltype(typed_value)>;
    output << typeid(ValueType).name() << ':';
    if constexpr (std::is_same_v<ValueType, std::monostate>) {
      output << "null";
    } else if constexpr (std::is_same_v<ValueType, DateValue>) {
      output << typed_value.year << '-' << typed_value.month << '-' << typed_value.day;
    } else if constexpr (std::is_same_v<ValueType, TimeValue>) {
      output << typed_value.hour << ':' << typed_value.minute << ':' << typed_value.second;
    } else if constexpr (std::is_same_v<ValueType, DateTimeValue>) {
      output << typed_value.date.year << '-' << typed_value.date.month << '-' << typed_value.date.day
             << ' ' << typed_value.time.hour << ':' << typed_value.time.minute << ':'
             << typed_value.time.second;
    } else {
      output << typed_value;
    }
  });
  return output.str();
}

std::vector<GeneratedValue> distinct_values(const std::vector<GeneratedValue>& values) {
  std::vector<GeneratedValue> distinct;
  std::unordered_set<std::string> seen;
  for (const auto& value : values) {
    if (seen.insert(value_key(value)).second) {
      distinct.push_back(value);
    }
  }
  return distinct;
}

std::string normalize_column_name(std::string column_name) {
  std::ranges::transform(column_name, column_name.begin(), [](unsigned char character) {
    if (std::isalnum(character) != 0) {
      return static_cast<char>(std::tolower(character));
    }
    return '_';
  });
  return column_name;
}

bool is_email_column(const std::string& column_name) {
  return column_name.find("email") != std::string::npos && !column_name.ends_with("_id") &&
         !column_name.ends_with("id");
}

std::string char_value_at(int row_index, int length) {
  int value = row_index;
  std::string result(static_cast<std::size_t>(length), 'A');
  for (int index = length - 1; index >= 0; --index) {
    result[static_cast<std::size_t>(index)] = static_cast<char>('A' + (value % 26));
    value /= 26;
  }
  return result;
}

std::int64_t integer_min_bound(const EffectiveCheckConstraint& check, double default_value) {
  if (!check.min_value.has_value()) {
    return static_cast<std::int64_t>(std::ceil(default_value));
  }

  const double value = check.min_value.value();
  if (check.min_inclusive) {
    return static_cast<std::int64_t>(std::ceil(value));
  }
  return static_cast<std::int64_t>(std::floor(value)) + 1;
}

std::int64_t integer_max_bound(const EffectiveCheckConstraint& check, double default_value) {
  if (!check.max_value.has_value()) {
    return static_cast<std::int64_t>(std::floor(default_value));
  }

  const double value = check.max_value.value();
  if (check.max_inclusive) {
    return static_cast<std::int64_t>(std::floor(value));
  }
  return static_cast<std::int64_t>(std::ceil(value)) - 1;
}

DateValue date_at(int row_index) {
  return DateValue{.year = 2026, .month = 1, .day = row_index + 1};
}

TimeValue time_at(int seconds) {
  return TimeValue{.hour = seconds / 3600, .minute = (seconds % 3600) / 60, .second = seconds % 60};
}

GeneratedValue generated_value_by_type(const Column& column, std::size_t row_index) {
  const DataType data_type = column.get_column_type().data_type;
  if (ColumnDomainResolver::is_integer_type(data_type)) {
    return GeneratedValue::integer(static_cast<std::int64_t>(row_index + 1));
  }

  if (ColumnDomainResolver::is_text_type(data_type)) {
    const std::string column_name = normalize_column_name(column.get_column_name());
    const std::string value = column_name + "_" + std::to_string(row_index + 1);
    if (is_email_column(column_name)) {
      return GeneratedValue::text(value + "@example.com");
    }
    return GeneratedValue::text(value);
  }

  if (ColumnDomainResolver::is_decimal_type(data_type)) {
    return GeneratedValue::numeric(static_cast<double>((row_index + 1) * 10) + 0.5);
  }

  if (data_type == DataType::BOOLEAN) {
    return GeneratedValue::boolean(row_index % 2 == 0);
  }

  if (data_type == DataType::CHAR) {
    const int length = ColumnDomainResolver::char_length(&column);
    const int capacity = ColumnDomainResolver::char_capacity(length);
    return GeneratedValue::text(char_value_at(static_cast<int>(row_index) % capacity, length));
  }

  if (data_type == DataType::DATE) {
    return GeneratedValue::date(date_at(static_cast<int>(row_index)));
  }

  if (data_type == DataType::TIME) {
    return GeneratedValue::time(time_at(static_cast<int>(row_index + 1)));
  }

  if (data_type == DataType::DATETIME) {
    return GeneratedValue::date_time(
        DateTimeValue{.date = DateValue{.year = 2026, .month = 1, .day = 1},
                      .time = time_at(static_cast<int>(row_index))});
  }

  throw std::runtime_error("Unsupported data type for column '" + column.get_column_name() + "'");
}

GeneratedValue generated_value_for_check(const Column& column, const EffectiveCheckConstraint& check,
                                         std::size_t row_index) {
  const DataType data_type = column.get_column_type().data_type;
  if (!check.allowed_values.empty()) {
    return ColumnDomainResolver::coerce_allowed_value(
        check.allowed_values[row_index % check.allowed_values.size()], data_type);
  }

  if (check.min_value.has_value() || check.max_value.has_value()) {
    if (ColumnDomainResolver::is_integer_type(data_type)) {
      const auto min_value = integer_min_bound(check, 1.0);
      const auto max_value =
          integer_max_bound(check, static_cast<double>(min_value + static_cast<int>(row_index)));
      const std::int64_t range_size = std::max<std::int64_t>(1, max_value - min_value + 1);
      return GeneratedValue::integer(min_value + (static_cast<std::int64_t>(row_index) % range_size));
    }

    if (ColumnDomainResolver::is_decimal_type(data_type)) {
      constexpr double decimal_step = 0.01;
      const double min_value =
          check.min_value.has_value()
              ? check.min_value.value() + (check.min_inclusive ? 0.0 : decimal_step)
              : 0.0;
      const double max_value =
          check.max_value.has_value()
              ? check.max_value.value() - (check.max_inclusive ? 0.0 : decimal_step)
              : min_value + (static_cast<double>(row_index + 1) * 10.5);
      const double range_width = std::max(0.0, max_value - min_value);
      double value = min_value + (static_cast<double>(row_index) * 10.5);
      if (value > max_value) {
        value = min_value + std::fmod(value - min_value, range_width == 0.0 ? 1.0 : range_width);
      }
      return GeneratedValue::numeric(value);
    }
  }

  return generated_value_by_type(column, row_index);
}

bool has_effective_domain(const EffectiveCheckConstraint& check) {
  return !check.allowed_values.empty() || check.min_value.has_value() || check.max_value.has_value();
}

bool should_generate_null(const Table& table, const Column& column, std::size_t row_index,
                          const GenerationConfig& config) {
  const ColumnGenerationConfig* column_config =
      config.get_column_config(table.get_table_name(), column.get_column_name());
  if (column_config == nullptr || !column_config->null_probability.has_value()) {
    return false;
  }
  return RealisticValueGenerator::deterministic_fraction(
             config.seed, table.get_table_name(), "null:" + column.get_column_name(), row_index) <
         column_config->null_probability.value();
}

std::string composite_key_name(const TableConstraint& constraint) {
  std::string name = constraint.type == ConstraintType::PrimaryKey ? "PRIMARY KEY(" : "UNIQUE(";
  for (std::size_t index = 0; index < constraint.columns.size(); ++index) {
    if (index > 0) {
      name += ", ";
    }
    name += constraint.columns[index] == nullptr ? "<unknown>"
                                                 : constraint.columns[index]->get_column_name();
  }
  name += ")";
  return name;
}

std::vector<GeneratedValue> composite_domain_for_column(const Table& table,
                                                        const ColumnData& column_data,
                                                        const GenerationConfig& config,
                                                        const KeyRegistry& key_registry) {
  if (const ForeignKey* foreign_key = find_single_column_foreign_key(table, column_data.column);
      foreign_key != nullptr) {
    const int parent_rows = config.get_row_count(foreign_key->referenced_table->get_table_name());
    std::vector<GeneratedValue> values;
    values.reserve(parent_rows);
    for (int row = 0; row < parent_rows; ++row) {
      values.push_back(key_registry
                           .key_at_row(foreign_key->referenced_table,
                                       foreign_key->referenced_columns,
                                       static_cast<std::size_t>(row))
                           .front());
    }
    return distinct_values(values);
  }

  return distinct_values(column_data.data);
}

std::string tuple_key(const std::vector<const ColumnData*>& columns, std::size_t row_index) {
  std::string key;
  for (const ColumnData* column_data : columns) {
    key += value_key(column_data->data[row_index]);
    key += "|";
  }
  return key;
}

void verify_composite_key_constraints(const TableData& table_data) {
  for (const auto& constraint : table_data.table->get_table_constraints()) {
    if (!is_composite_key_constraint(constraint)) {
      continue;
    }

    std::vector<const ColumnData*> columns;
    columns.reserve(constraint.columns.size());
    for (const Column* column : constraint.columns) {
      const ColumnData* column_data = find_column_data(table_data.columns, column);
      if (column_data != nullptr) {
        columns.push_back(column_data);
      }
    }

    if (columns.size() != constraint.columns.size() || columns.empty()) {
      continue;
    }

    const std::size_t row_count = columns.front()->data.size();
    std::unordered_set<std::string> seen;
    for (std::size_t row = 0; row < row_count; ++row) {
      if (!seen.insert(tuple_key(columns, row)).second) {
        throw std::runtime_error("Generated duplicate tuple for composite " +
                                 composite_key_name(constraint) + " on table '" +
                                 table_data.table->get_table_name() + "'");
      }
    }
  }
}

void apply_composite_foreign_keys(const Table& table, int num_rows, RandomEngine& random_engine,
                                  const KeyRegistry& key_registry,
                                  std::vector<ColumnData>& columns) {
  if (num_rows <= 0) {
    return;
  }

  for (const auto& foreign_key : table.get_foreign_keys()) {
    if (!is_composite_foreign_key(foreign_key)) {
      continue;
    }

    if (foreign_key.local_columns.size() != foreign_key.referenced_columns.size()) {
      throw std::runtime_error("Composite foreign key on table '" + table.get_table_name() +
                               "' has mismatched local and referenced column counts");
    }

    std::vector<ColumnData*> local_columns;
    local_columns.reserve(foreign_key.local_columns.size());
    for (const Column* column : foreign_key.local_columns) {
      ColumnData* column_data = find_column_data(columns, column);
      if (column_data == nullptr) {
        throw std::runtime_error("Composite foreign key on table '" + table.get_table_name() +
                                 "' references missing generated column '" +
                                 (column == nullptr ? std::string("<unknown>")
                                                    : column->get_column_name()) +
                                 "'");
      }
      local_columns.push_back(column_data);
    }

    for (int row = 0; row < num_rows; ++row) {
      const auto parent_key = key_registry.random_key(foreign_key.referenced_table,
                                                      foreign_key.referenced_columns,
                                                      random_engine);
      if (parent_key.size() != local_columns.size()) {
        throw std::runtime_error("Composite foreign key on table '" + table.get_table_name() +
                                 "' expected " + std::to_string(local_columns.size()) +
                                 " values from referenced key but got " +
                                 std::to_string(parent_key.size()));
      }

      for (std::size_t column_index = 0; column_index < local_columns.size(); ++column_index) {
        local_columns[column_index]->data[static_cast<std::size_t>(row)] =
            parent_key[column_index];
      }
    }
  }
}

void apply_composite_key_constraints(const Table& table, int num_rows,
                                     const GenerationConfig& config,
                                     const KeyRegistry& key_registry,
                                     std::vector<ColumnData>& columns) {
  if (num_rows <= 0) {
    return;
  }

  for (const auto& constraint : table.get_table_constraints()) {
    if (!is_composite_key_constraint(constraint)) {
      continue;
    }
    if (constraint.type == ConstraintType::Unique && has_already_unique_member(table, constraint)) {
      continue;
    }

    std::vector<ColumnData*> constrained_columns;
    std::vector<std::vector<GeneratedValue>> domains;
    constrained_columns.reserve(constraint.columns.size());
    domains.reserve(constraint.columns.size());
    for (const Column* column : constraint.columns) {
      ColumnData* column_data = find_column_data(columns, column);
      if (column_data == nullptr) {
        continue;
      }

      auto domain = composite_domain_for_column(table, *column_data, config, key_registry);
      if (domain.empty()) {
        throw std::runtime_error("Composite " + composite_key_name(constraint) +
                                 " on table '" + table.get_table_name() +
                                 "' has no values for column '" + column->get_column_name() + "'");
      }

      constrained_columns.push_back(column_data);
      domains.push_back(std::move(domain));
    }

    if (constrained_columns.size() != constraint.columns.size()) {
      continue;
    }

    for (int row = 0; row < num_rows; ++row) {
      std::size_t divisor = 1;
      for (std::size_t column_index = 0; column_index < constrained_columns.size(); ++column_index) {
        divisor = 1;
        for (std::size_t later = column_index + 1; later < domains.size(); ++later) {
          divisor *= domains[later].size();
        }
        const std::size_t value_index =
            (static_cast<std::size_t>(row) / divisor) % domains[column_index].size();
        constrained_columns[column_index]->data[static_cast<std::size_t>(row)] =
            domains[column_index][value_index];
      }
    }
  }
}

std::size_t column_index_or_throw(const GeneratedRow& row, const Column* column,
                                  const std::string& context) {
  for (std::size_t index = 0; index < row.columns.size(); ++index) {
    if (row.columns[index] == column) {
      return index;
    }
  }
  throw std::runtime_error(context + " references missing generated column '" +
                           (column == nullptr ? std::string("<unknown>")
                                              : column->get_column_name()) +
                           "'");
}

GeneratedValue generate_base_row_value(const Table& table, const Column& column,
                                       std::size_t row_index, RandomEngine& random_engine,
                                       const KeyRegistry& key_registry,
                                       const GenerationConfig& config) {
  const DataType data_type = column.get_column_type().data_type;
  if (has_single_column_constraint(table, &column, ConstraintType::PrimaryKey)) {
    std::vector<Column*> key_columns =
        key_columns_for_single_constraint(table, &column, ConstraintType::PrimaryKey);
    return key_registry.key_at_row(&table, key_columns, row_index).front();
  }

  if (should_generate_null(table, column, row_index, config)) {
    return GeneratedValue::null();
  }

  if (const ForeignKey* foreign_key = find_foreign_key(table, &column); foreign_key != nullptr) {
    if (!ColumnDomainResolver::is_integer_type(data_type) &&
        !ColumnDomainResolver::is_text_type(data_type)) {
      throw std::runtime_error("Foreign key column '" + column.get_column_name() +
                               "' must use an integer or text type for generation");
    }

    if (!is_composite_foreign_key(*foreign_key)) {
      if (has_single_column_constraint(table, &column, ConstraintType::Unique)) {
        return key_registry
            .key_at_row(foreign_key->referenced_table, foreign_key->referenced_columns, row_index)
            .front();
      }
      return key_registry
          .random_key(foreign_key->referenced_table, foreign_key->referenced_columns,
                      random_engine)
          .front();
    }
  }


  if (has_single_column_constraint(table, &column, ConstraintType::Unique) &&
      (ColumnDomainResolver::is_integer_type(data_type) ||
       ColumnDomainResolver::is_text_type(data_type) || data_type == DataType::CHAR)) {
    std::vector<Column*> key_columns =
        key_columns_for_single_constraint(table, &column, ConstraintType::Unique);
    return key_registry.key_at_row(&table, key_columns, row_index).front();
  }

  const EffectiveCheckConstraint effective_check =
      ColumnDomainResolver::effective_check_for_column(table, column, config);
  if (has_effective_domain(effective_check)) {
    return generated_value_for_check(column, effective_check, row_index);
  }
  if (auto realistic = RealisticValueGenerator::generate(table, column, row_index, config);
      realistic.has_value()) {
    return realistic.value();
  }
  return generated_value_by_type(column, row_index);
}

std::vector<GeneratedValue> domain_for_streaming_column(const Table& table, const Column* column,
                                                       int num_rows,
                                                       const GenerationConfig& config,
                                                       const KeyRegistry& key_registry) {
  if (const ForeignKey* foreign_key = find_single_column_foreign_key(table, column);
      foreign_key != nullptr) {
    const int parent_rows = config.get_row_count(foreign_key->referenced_table->get_table_name());
    std::vector<GeneratedValue> values;
    values.reserve(static_cast<std::size_t>(std::max(0, parent_rows)));
    for (int row = 0; row < parent_rows; ++row) {
      values.push_back(key_registry
                           .key_at_row(foreign_key->referenced_table,
                                       foreign_key->referenced_columns,
                                       static_cast<std::size_t>(row))
                           .front());
    }
    return distinct_values(values);
  }

  std::vector<GeneratedValue> values;
  values.reserve(static_cast<std::size_t>(std::max(0, num_rows)));
  for (int row = 0; row < num_rows; ++row) {
    const EffectiveCheckConstraint effective_check =
        ColumnDomainResolver::effective_check_for_column(table, *column, config);
    if (has_effective_domain(effective_check)) {
      values.push_back(
          generated_value_for_check(*column, effective_check, static_cast<std::size_t>(row)));
    } else if (auto realistic = RealisticValueGenerator::generate(
                   table, *column, static_cast<std::size_t>(row), config, true);
               realistic.has_value()) {
      values.push_back(realistic.value());
    } else {
      values.push_back(generated_value_by_type(*column, static_cast<std::size_t>(row)));
    }
  }
  return distinct_values(values);
}

struct CompositeAssignment {
  const TableConstraint* constraint;
  std::vector<std::size_t> column_indexes;
  std::vector<std::vector<GeneratedValue>> domains;
};

std::vector<CompositeAssignment> make_composite_assignments(const Table& table, int num_rows,
                                                           const GenerationConfig& config,
                                                           const KeyRegistry& key_registry,
                                                           const GeneratedRow& row) {
  std::vector<CompositeAssignment> assignments;
  for (const auto& constraint : table.get_table_constraints()) {
    if (!is_composite_key_constraint(constraint)) {
      continue;
    }
    if (constraint.type == ConstraintType::Unique && has_already_unique_member(table, constraint)) {
      continue;
    }

    CompositeAssignment assignment{.constraint = &constraint, .column_indexes = {}, .domains = {}};
    assignment.column_indexes.reserve(constraint.columns.size());
    assignment.domains.reserve(constraint.columns.size());

    for (const Column* column : constraint.columns) {
      assignment.column_indexes.push_back(
          column_index_or_throw(row, column, "Composite " + composite_key_name(constraint) +
                                                 " on table '" + table.get_table_name() + "'"));
      auto domain = domain_for_streaming_column(table, column, num_rows, config, key_registry);
      if (domain.empty()) {
        throw std::runtime_error("Composite " + composite_key_name(constraint) + " on table '" +
                                 table.get_table_name() + "' has no values for column '" +
                                 column->get_column_name() + "'");
      }
      assignment.domains.push_back(std::move(domain));
    }
    assignments.push_back(std::move(assignment));
  }
  return assignments;
}

void apply_composite_foreign_keys_to_row(const Table& table, GeneratedRow& row,
                                         std::size_t row_index,
                                         const GenerationConfig& config,
                                         RandomEngine& random_engine,
                                         const KeyRegistry& key_registry) {
  for (const auto& foreign_key : table.get_foreign_keys()) {
    if (!is_composite_foreign_key(foreign_key)) {
      continue;
    }

    if (foreign_key.local_columns.size() != foreign_key.referenced_columns.size()) {
      throw std::runtime_error("Composite foreign key on table '" + table.get_table_name() +
                               "' has mismatched local and referenced column counts");
    }

    const auto nullable_column = std::ranges::find_if(
        foreign_key.local_columns, [&table, &config](const Column* column) {
          const auto* column_config =
              config.get_column_config(table.get_table_name(), column->get_column_name());
          return column_config != nullptr && column_config->null_probability.has_value();
        });
    if (nullable_column != foreign_key.local_columns.end() &&
        should_generate_null(table, **nullable_column, row_index, config)) {
      for (const Column* local_column : foreign_key.local_columns) {
        row.values[column_index_or_throw(row, local_column,
                                         "Composite foreign key on table '" +
                                             table.get_table_name() + "'")] =
            GeneratedValue::null();
      }
      continue;
    }

    const auto parent_key =
        key_registry.random_key(foreign_key.referenced_table, foreign_key.referenced_columns,
                                random_engine);
    if (parent_key.size() != foreign_key.local_columns.size()) {
      throw std::runtime_error("Composite foreign key on table '" + table.get_table_name() +
                               "' expected " +
                               std::to_string(foreign_key.local_columns.size()) +
                               " values from referenced key but got " +
                               std::to_string(parent_key.size()));
    }

    for (std::size_t column_index = 0; column_index < foreign_key.local_columns.size();
         ++column_index) {
      const std::size_t row_column_index = column_index_or_throw(
          row, foreign_key.local_columns[column_index],
          "Composite foreign key on table '" + table.get_table_name() + "'");
      row.values[row_column_index] = parent_key[column_index];
    }
  }
}

void apply_composite_assignments_to_row(const std::vector<CompositeAssignment>& assignments,
                                        std::size_t row_index, GeneratedRow& row) {
  for (const auto& assignment : assignments) {
    for (std::size_t column_index = 0; column_index < assignment.column_indexes.size();
         ++column_index) {
      std::size_t divisor = 1;
      for (std::size_t later = column_index + 1; later < assignment.domains.size(); ++later) {
        divisor *= assignment.domains[later].size();
      }
      const std::size_t value_index = (row_index / divisor) % assignment.domains[column_index].size();
      row.values[assignment.column_indexes[column_index]] =
          assignment.domains[column_index][value_index];
    }
  }
}

GeneratedRow generate_streaming_row(const Table& table, int num_rows, std::size_t row_index,
                                    const GenerationConfig& config, RandomEngine& random_engine,
                                    const KeyRegistry& key_registry,
                                    const std::vector<CompositeAssignment>& assignments) {
  (void)num_rows;
  (void)config;
  GeneratedRow row{.table = &table, .columns = {}, .values = {}};
  const auto& columns = table.get_columns();
  row.columns.reserve(columns.size());
  row.values.reserve(columns.size());
  for (const auto& column_ptr : columns) {
    row.columns.push_back(column_ptr.get());
    row.values.push_back(
        generate_base_row_value(table, *column_ptr, row_index, random_engine, key_registry, config));
  }

  apply_composite_foreign_keys_to_row(table, row, row_index, config, random_engine, key_registry);
  apply_composite_assignments_to_row(assignments, row_index, row);
  return row;
}

}  // namespace

std::vector<ColumnData> GenerationPlan::generate_columns_data(const Table& table, int num_rows,
                                                              const GenerationConfig& config,
                                                              RandomEngine& random_engine,
                                                              const KeyRegistry& key_registry) {
  std::vector<ColumnData> column_data;
  const auto& columns = table.get_columns();
  column_data.reserve(columns.size());
  for (const auto& column_ptr : columns) {
    const Column* column = column_ptr.get();
    column_data.push_back(ColumnData{
        .column = column,
        .data = std::move(ValueGenerator::generate_column_data(
            *column, table, num_rows, config, random_engine, key_registry))});
  }
  apply_composite_foreign_keys(table, num_rows, random_engine, key_registry, column_data);
  apply_composite_key_constraints(table, num_rows, config, key_registry, column_data);
  return column_data;
}

std::vector<TableData> GenerationPlan::generate_table_data(const std::vector<TablePtr>& tables,
                                                           int num_rows) {
  GenerationConfig config;
  config.default_num_rows = num_rows;
  for (const auto& table_ptr : tables) {
    config.table_row_counts[table_ptr->get_table_name()] = num_rows;
  }

  return generate_table_data(tables, config);
}

std::vector<TableData> GenerationPlan::generate_table_data(const std::vector<TablePtr>& tables,
                                                           const GenerationConfig& config) {
  std::vector<TableData> table_data;
  table_data.reserve(tables.size());
  stream_table_data(
      tables, config,
      GenerationStreamConsumer{
          .table_started = [&table_data](const Table& table) {
            TableData data{.table = &table, .columns = {}};
            data.columns.reserve(table.get_columns().size());
            for (const auto& column : table.get_columns()) {
              data.columns.push_back(ColumnData{.column = column.get(), .data = {}});
            }
            table_data.push_back(std::move(data));
          },
          .row_generated = [&table_data](const GeneratedRow& row) {
            TableData& data = table_data.back();
            for (std::size_t index = 0; index < row.values.size(); ++index) {
              data.columns[index].data.push_back(row.values[index]);
            }
          },
          .table_finished = [&table_data](const Table&) {
            verify_composite_key_constraints(table_data.back());
          }});
  return table_data;
}

std::vector<TableData> GenerationPlan::generate_table_data(
    const std::vector<TablePtr>& tables, const std::unordered_map<std::string, int>& row_counts,
    int default_num_rows) {
  GenerationConfig config;
  config.default_num_rows = default_num_rows;
  config.table_row_counts = row_counts;
  return generate_table_data(tables, config);
}

void GenerationPlan::stream_table_data(
    const std::vector<TablePtr>& tables, const GenerationConfig& config,
    const std::function<void(const GeneratedRow&)>& row_consumer) {
  stream_table_data(tables, config,
                    GenerationStreamConsumer{.table_started = {},
                                             .row_generated = row_consumer,
                                             .table_finished = {}});
}

void GenerationPlan::stream_table_data(const std::vector<TablePtr>& tables,
                                       const GenerationConfig& config,
                                       const GenerationStreamConsumer& consumer) {
  RandomEngine random_engine(config.seed);
  const KeyRegistry key_registry = KeyRegistry::build_from_tables(tables, config);

  for (const auto& table_ptr : tables) {
    const Table* table = table_ptr.get();
    const int num_rows = config.get_row_count(table->get_table_name());
    if (num_rows < 0) {
      throw std::runtime_error("Row count cannot be negative for table '" + table->get_table_name() +
                               "'");
    }

    if (consumer.table_started) {
      consumer.table_started(*table);
    }

    GeneratedRow row_layout{.table = table, .columns = {}, .values = {}};
    row_layout.columns.reserve(table->get_columns().size());
    for (const auto& column_ptr : table->get_columns()) {
      row_layout.columns.push_back(column_ptr.get());
    }

    const auto composite_assignments =
        make_composite_assignments(*table, num_rows, config, key_registry, row_layout);
    for (int row_index = 0; row_index < num_rows; ++row_index) {
      if (consumer.row_generated) {
        consumer.row_generated(generate_streaming_row(
            *table, num_rows, static_cast<std::size_t>(row_index), config, random_engine,
            key_registry, composite_assignments));
      }
    }

    if (consumer.table_finished) {
      consumer.table_finished(*table);
    }
  }
}

std::ostream& operator<<(std::ostream& os, const ColumnData& column_data) {
  os << "ColumnData(column: " << *column_data.column << ", data: [";
  for (const auto& value : column_data.data) {
    os << value << ", ";
  }
  os << "])";
  return os;
}

std::ostream& operator<<(std::ostream& os, const TableData& table_data) {
  os << table_data.table->get_table_name() << '\n';

  for (std::size_t column_index = 0; column_index < table_data.columns.size(); ++column_index) {
    if (column_index > 0) {
      os << " | ";
    }
    os << table_data.columns[column_index].column->get_column_name();
  }
  os << '\n';

  std::size_t row_count = 0;
  if (!table_data.columns.empty()) {
    row_count = table_data.columns.front().data.size();
  }

  for (std::size_t row_index = 0; row_index < row_count; ++row_index) {
    for (std::size_t column_index = 0; column_index < table_data.columns.size(); ++column_index) {
      if (column_index > 0) {
        os << " | ";
      }

      const auto& column_data = table_data.columns[column_index].data;
      if (row_index < column_data.size()) {
        os << column_data[row_index];
      }
    }

    if (row_index + 1 < row_count) {
      os << '\n';
    }
  }

  return os;
}

}  // namespace schemaforge
