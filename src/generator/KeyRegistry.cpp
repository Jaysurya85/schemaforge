#include "schemaforge/generator/KeyRegistry.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <functional>
#include <stdexcept>
#include <type_traits>
#include <utility>

#include "schemaforge/domain/ColumnDomainResolver.h"

namespace schemaforge {

namespace {

bool is_email_column(std::string column_name) {
  std::ranges::transform(column_name, column_name.begin(), [](unsigned char character) {
    return static_cast<char>(std::tolower(character));
  });
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

bool is_single_key_constraint(const TableConstraint& constraint) {
  if (constraint.columns.size() != 1) {
    return false;
  }

  if (constraint.type != ConstraintType::PrimaryKey && constraint.type != ConstraintType::Unique) {
    return false;
  }

  const Column* column = constraint.columns.front();
  return column != nullptr;
}

std::string key_name(const Table* table) {
  if (table == nullptr) {
    return "<null table>";
  }
  return table->get_table_name();
}

}  // namespace

bool KeyRegistry::KeyRef::operator==(const KeyRef& other) const {
  return table == other.table && columns == other.columns;
}

std::size_t KeyRegistry::KeyRefHash::operator()(const KeyRef& key) const {
  std::size_t hash = std::hash<const Table*>{}(key.table);
  for (const Column* column : key.columns) {
    const std::size_t column_hash = std::hash<const Column*>{}(column);
    hash ^= column_hash + 0x9e3779b9 + (hash << 6) + (hash >> 2);
  }
  return hash;
}

KeyRegistry::KeyRef KeyRegistry::make_key(const Table* table, const std::vector<Column*>& columns) {
  std::vector<const Column*> key_columns;
  key_columns.reserve(columns.size());
  for (const Column* column : columns) {
    key_columns.push_back(column);
  }
  return KeyRef{.table = table, .columns = std::move(key_columns)};
}

void KeyRegistry::register_int_range(const Table* table, const std::vector<Column*>& columns,
                                     int start, int count) {
  key_sources[make_key(table, columns)] = IntRangeKeySource{.start = start, .count = count};
}

void KeyRegistry::register_pattern(const Table* table, const std::vector<Column*>& columns,
                                   PatternKeyKind kind, std::string prefix, int length,
                                   int count) {
  key_sources[make_key(table, columns)] =
      PatternKeySource{.kind = kind, .prefix = std::move(prefix), .length = length, .count = count};
}

GeneratedValue KeyRegistry::value_at_row(const PatternKeySource& source, std::size_t row_index) {
  if (row_index >= static_cast<std::size_t>(source.count)) {
    throw std::runtime_error("Referenced pattern key source does not have enough values");
  }

  switch (source.kind) {
    case PatternKeyKind::Email:
      return GeneratedValue::text("email_" + std::to_string(row_index + 1) + "@example.com");
    case PatternKeyKind::Char:
      return GeneratedValue::text(char_value_at(static_cast<int>(row_index), source.length));
    case PatternKeyKind::ColumnKey:
      return GeneratedValue::text(source.prefix + "_" + std::to_string(row_index + 1));
    case PatternKeyKind::TableKey:
    default:
      return GeneratedValue::text(source.prefix + "_key_" + std::to_string(row_index + 1));
  }
}

std::vector<GeneratedValue> KeyRegistry::random_key(const Table* table,
                                                    const std::vector<Column*>& columns,
                                                    RandomEngine& random_engine) const {
  const auto key_source = key_sources.find(make_key(table, columns));
  if (key_source == key_sources.end()) {
    throw std::runtime_error("No key source registered for referenced key '" + key_name(table) +
                             "'");
  }

  return std::visit(
      [&](const auto& source) -> std::vector<GeneratedValue> {
        if constexpr (std::is_same_v<std::decay_t<decltype(source)>, IntRangeKeySource>) {
          if (source.count <= 0) {
            throw std::runtime_error("Referenced key source for table '" + key_name(table) +
                                     "' has no values");
          }
          const int offset = random_engine.next_int(0, source.count - 1);
          return {GeneratedValue::integer(source.start + offset)};
        } else if constexpr (std::is_same_v<std::decay_t<decltype(source)>, PatternKeySource>) {
          if (source.count <= 0) {
            throw std::runtime_error("Referenced key source for table '" + key_name(table) +
                                     "' has no values");
          }
          const int offset = random_engine.next_int(0, source.count - 1);
          return {value_at_row(source, static_cast<std::size_t>(offset))};
        } else {
          if (source.values.empty()) {
            throw std::runtime_error("Referenced key source for table '" + key_name(table) +
                                     "' has no values");
          }
          const int offset = random_engine.next_int(0, static_cast<int>(source.values.size()) - 1);
          return {source.values[static_cast<std::size_t>(offset)]};
        }
      },
      key_source->second);
}

std::vector<GeneratedValue> KeyRegistry::key_at_row(const Table* table,
                                                    const std::vector<Column*>& columns,
                                                    std::size_t row_index) const {
  const auto key_source = key_sources.find(make_key(table, columns));
  if (key_source == key_sources.end()) {
    throw std::runtime_error("No key source registered for referenced key '" + key_name(table) +
                             "'");
  }

  return std::visit(
      [&](const auto& source) -> std::vector<GeneratedValue> {
        if constexpr (std::is_same_v<std::decay_t<decltype(source)>, IntRangeKeySource>) {
          if (row_index >= static_cast<std::size_t>(source.count)) {
            throw std::runtime_error("Referenced key source for table '" + key_name(table) +
                                     "' does not have enough values");
          }
          return {GeneratedValue::integer(source.start + static_cast<int>(row_index))};
        } else if constexpr (std::is_same_v<std::decay_t<decltype(source)>, PatternKeySource>) {
          if (row_index >= static_cast<std::size_t>(source.count)) {
            throw std::runtime_error("Referenced key source for table '" + key_name(table) +
                                     "' does not have enough values");
          }
          return {value_at_row(source, row_index)};
        } else {
          if (row_index >= source.values.size()) {
            throw std::runtime_error("Referenced key source for table '" + key_name(table) +
                                     "' does not have enough values");
          }
          return {source.values[row_index]};
        }
      },
      key_source->second);
}

KeyRegistry KeyRegistry::build_from_tables(const std::vector<TablePtr>& tables,
                                           const GenerationConfig& config) {
  KeyRegistry key_registry;

  for (const auto& table_ptr : tables) {
    const Table* table = table_ptr.get();
    const int row_count = config.get_row_count(table->get_table_name());
    for (const auto& constraint : table->get_table_constraints()) {
      if (!is_single_key_constraint(constraint)) {
        continue;
      }

      const Column* column = constraint.columns.front();
      const DataType data_type = column->get_column_type().data_type;
      const EffectiveCheckConstraint effective_check =
          ColumnDomainResolver::effective_check_for_column(table, column);
      if (!effective_check.allowed_values.empty()) {
        std::vector<GeneratedValue> values;
        values.reserve(effective_check.allowed_values.size());
        for (const auto& value : effective_check.allowed_values) {
          values.push_back(ColumnDomainResolver::coerce_allowed_value(value, data_type));
        }
        key_registry.key_sources[make_key(table, constraint.columns)] =
            AllowedValuesKeySource{.values = std::move(values)};
        continue;
      }

      if (ColumnDomainResolver::is_integer_type(data_type)) {
        const int start =
            static_cast<int>(std::ceil(effective_check.min_value.value_or(1.0)));
        const int end =
            static_cast<int>(std::floor(effective_check.max_value.value_or(start + row_count - 1)));
        const int count = std::max(0, end - start + 1);
        key_registry.register_int_range(table, constraint.columns, start, count);
        continue;
      }

      if (ColumnDomainResolver::is_text_type(data_type)) {
        const bool email_column = is_email_column(column->get_column_name());
        const PatternKeyKind kind =
            email_column ? PatternKeyKind::Email
                         : constraint.type == ConstraintType::PrimaryKey ? PatternKeyKind::TableKey
                                                                         : PatternKeyKind::ColumnKey;
        const std::string prefix =
            kind == PatternKeyKind::TableKey ? table->get_table_name() : column->get_column_name();
        key_registry.register_pattern(table, constraint.columns, kind, prefix, 0, row_count);
        continue;
      }

      if (data_type == DataType::CHAR) {
        key_registry.register_pattern(table, constraint.columns, PatternKeyKind::Char,
                                      table->get_table_name(),
                                      ColumnDomainResolver::char_length(column), row_count);
      }
    }
  }

  return key_registry;
}

}  // namespace schemaforge
