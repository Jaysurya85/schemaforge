#include "schemaforge/validation/CapacityAnalyzer.h"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <optional>

#include "schemaforge/domain/ColumnDomainResolver.h"

namespace schemaforge {

bool CapacityAnalyzer::contains_column(const std::vector<Column*>& columns, const Column* column) {
  return std::ranges::find(columns, column) != columns.end();
}

bool CapacityAnalyzer::has_constraint(const Table* table, const Column* column,
                                      ConstraintType constraint_type) {
  for (const auto& constraint : table->get_table_constraints()) {
    if (constraint.type == constraint_type && constraint.columns.size() == 1 &&
        contains_column(constraint.columns, column)) {
      return true;
    }
  }
  return false;
}

std::optional<RowCapacityLimit> check_capacity_limit(const Table* table, const Column* column) {
  auto has_local_constraint = [table, column](ConstraintType type) {
    for (const auto& constraint : table->get_table_constraints()) {
      if (constraint.type == type && constraint.columns.size() == 1 &&
          std::ranges::find(constraint.columns, column) != constraint.columns.end()) {
        return true;
      }
    }
    return false;
  };
  const bool has_unique = has_local_constraint(ConstraintType::Unique);
  const bool has_primary_key = has_local_constraint(ConstraintType::PrimaryKey);
  if (!has_unique && !has_primary_key) {
    return std::nullopt;
  }

  GenerationConfig config;
  const ColumnDomain domain = ColumnDomainResolver::domain_for_column(table, column, config, 0);
  const std::string qualified_column = table->get_table_name() + "." + column->get_column_name();
  if (!domain.check.allowed_values.empty() && domain.finite_capacity.has_value()) {
    return RowCapacityLimit{.max_rows = domain.finite_capacity.value(),
                            .reason = "Column " + qualified_column +
                                      " is UNIQUE CHECK and can only produce " +
                                      std::to_string(domain.finite_capacity.value()) +
                                      " distinct values."};
  }

  if (ColumnDomainResolver::is_integer_type(column->get_column_type().data_type) &&
      domain.check.min_value.has_value() && domain.check.max_value.has_value() &&
      domain.finite_capacity.has_value()) {
    return RowCapacityLimit{.max_rows = domain.finite_capacity.value(),
                            .reason = "Column " + qualified_column +
                                      " is UNIQUE CHECK and can only produce " +
                                      std::to_string(domain.finite_capacity.value()) +
                                      " distinct values."};
  }

  return std::nullopt;
}

const ForeignKey* find_single_column_foreign_key(const Table* table, const Column* column) {
  for (const auto& foreign_key : table->get_foreign_keys()) {
    if (foreign_key.local_columns.size() == 1 && foreign_key.referenced_columns.size() == 1 &&
        foreign_key.local_columns.front() == column) {
      return &foreign_key;
    }
  }
  return nullptr;
}

int configured_rows_for_table(const GenerationConfig& config, const Table* table) {
  if (table == nullptr) {
    return 0;
  }
  return config.get_row_count(table->get_table_name());
}

int composite_column_domain_size(const Table* table, const Column* column,
                                 const GenerationConfig& config, int requested_rows) {
  if (const ForeignKey* foreign_key = find_single_column_foreign_key(table, column);
      foreign_key != nullptr) {
    return configured_rows_for_table(config, foreign_key->referenced_table);
  }

  const ColumnDomain domain =
      ColumnDomainResolver::domain_for_column(table, column, config, requested_rows);
  if (domain.finite_capacity.has_value()) {
    return domain.finite_capacity.value();
  }

  return requested_rows;
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

std::optional<RowCapacityLimit> composite_key_capacity_limit(const Table* table,
                                                            const TableConstraint& constraint,
                                                            const GenerationConfig& config,
                                                            int requested_rows) {
  if ((constraint.type != ConstraintType::Unique && constraint.type != ConstraintType::PrimaryKey) ||
      constraint.columns.size() <= 1) {
    return std::nullopt;
  }

  std::int64_t capacity = 1;
  for (const Column* column : constraint.columns) {
    if (column == nullptr) {
      return std::nullopt;
    }

    const int domain_size = composite_column_domain_size(table, column, config, requested_rows);
    if (domain_size <= 0) {
      capacity = 0;
      break;
    }
    if (capacity > std::numeric_limits<int>::max() / domain_size) {
      capacity = std::numeric_limits<int>::max();
      break;
    }
    capacity *= domain_size;
  }

  return RowCapacityLimit{
      .max_rows = static_cast<int>(std::min<std::int64_t>(capacity, std::numeric_limits<int>::max())),
      .reason = "Composite " + composite_key_name(constraint) + " on table '" +
                table->get_table_name() + "' can only produce " + std::to_string(capacity) +
                " distinct tuples."};
}

std::optional<RowCapacityLimit> CapacityAnalyzer::column_capacity_limit(const Table* table,
                                                                        const Column* column) {
  auto check_limit = check_capacity_limit(table, column);
  if (check_limit.has_value()) {
    return check_limit;
  }

  if (has_constraint(table, column, ConstraintType::Unique) &&
      column->get_column_type().data_type == DataType::BOOLEAN) {
    return RowCapacityLimit{.max_rows = 2,
                            .reason = "UNIQUE BOOLEAN column '" + table->get_table_name() + "." +
                                      column->get_column_name() +
                                      "' because BOOLEAN has only 2 possible values"};
  }

  if (column->get_column_type().data_type == DataType::CHAR) {
    const bool is_unique = has_constraint(table, column, ConstraintType::Unique);
    const bool is_primary_key = has_constraint(table, column, ConstraintType::PrimaryKey);
    if (is_unique || is_primary_key) {
      const int length = ColumnDomainResolver::char_length(column);
      const int capacity = ColumnDomainResolver::char_capacity(length);
      const std::string qualified_column_name =
          table->get_table_name() + "." + column->get_column_name();
      const std::string constraint_name = is_primary_key ? "PRIMARY KEY" : "UNIQUE";
      return RowCapacityLimit{.max_rows = capacity,
                              .reason = "Column " + qualified_column_name + " is " +
                                        constraint_name + " CHAR(" + std::to_string(length) +
                                        ") and can only produce " + std::to_string(capacity) +
                                        " distinct values."};
    }
  }

  return std::nullopt;
}

void CapacityAnalyzer::apply_capacity_limit(TableCapacityInfo& table_info,
                                            const RowCapacityLimit& capacity_limit) {
  table_info.reasons.push_back(capacity_limit.reason);
  table_info.max_rows = std::min(table_info.max_rows, capacity_limit.max_rows);
}

SchemaCapacityInfo CapacityAnalyzer::analyze(const std::vector<TablePtr>& tables,
                                             const GenerationConfig& config) {
  SchemaCapacityInfo capacity_info;
  capacity_info.tables.reserve(tables.size());

  for (const auto& table_ptr : tables) {
    const TableId table_id = table_ptr->get_table_name();
    TableCapacityInfo table_info{.table = table_ptr.get(),
                                 .table_id = table_id,
                                 .requested_rows = config.get_row_count(table_id),
                                 .max_rows = std::numeric_limits<int>::max(),
                                 .reasons = {}};

    apply_capacity_limit(table_info,
                         RowCapacityLimit{.max_rows = table_info.requested_rows,
                                          .reason = "requested row count for table '" + table_id +
                                                    "'"});

    for (const auto& column_ptr : table_ptr->get_columns()) {
      auto column_limit = column_capacity_limit(table_ptr.get(), column_ptr.get());
      if (column_limit.has_value()) {
        apply_capacity_limit(table_info, column_limit.value());
      }
    }

    for (const auto& constraint : table_ptr->get_table_constraints()) {
      auto composite_limit =
          composite_key_capacity_limit(table_ptr.get(), constraint, config, table_info.requested_rows);
      if (composite_limit.has_value()) {
        apply_capacity_limit(table_info, composite_limit.value());
      }
    }

    capacity_info.tables.push_back(std::move(table_info));
  }

  return capacity_info;
}

}  // namespace schemaforge
