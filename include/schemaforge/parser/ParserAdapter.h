#pragma once

#include <string>
#include <unordered_map>
#include <vector>

#include "schemaforge/schema/Column.h"
#include "schemaforge/schema/Table.h"
#include "sql/ColumnType.h"
#include "sql/CreateStatement.h"

namespace schemaforge {

class ParserAdapter {
 private:
  static DataType convert_data_type(hsql::DataType data_type);

  static ColumnType convert_column_type(const hsql::ColumnType& hsql_column_type);

  static ConstraintType convert_constraint_type(const hsql::ConstraintType& constraint_type);

  static TableConstraint convert_table_constraint(const hsql::TableConstraint* table_constraint,
                                                  const std::vector<ColumnPtr>& columns);

  static Column* find_column_by_name(const std::string& name,
                                     const std::vector<ColumnPtr>& columns);

  static Table* find_table_by_name(const std::string& name, const std::vector<TablePtr>& tables);

  static std::vector<TableConstraint> extract_table_constraints(
      const hsql::CreateStatement* create_stmt, const std::vector<ColumnPtr>& columns);

  [[nodiscard]] static bool should_store_as_table_constraint(TableConstraint& table_contraint);

  static std::vector<TableConstraint> extract_column_constraints(
      const hsql::CreateStatement* create_stmt, const std::vector<ColumnPtr>& columns);

  static std::vector<TableConstraint> convert_constraints(const hsql::CreateStatement* create_stmt,
                                                          const std::vector<ColumnPtr>& columns);

  static ForeignKeySpec convert_foreign_key_spec(
      const hsql::ForeignKeyConstraint* foreign_key_constraint);

  static std::vector<ForeignKeySpec> extract_table_foreign_keys_spec(
      const hsql::CreateStatement* create_stmt);

  static std::vector<ForeignKeySpec> extract_column_foreign_keys_spec(
      const hsql::CreateStatement* create_stmt);

  static std::vector<ForeignKeySpec> extract_foreign_keys_spec(
      const hsql::CreateStatement* create_stmt);

  static std::vector<std::string> convert_names(const std::vector<char*>* names);

  static Column convert_column(const hsql::ColumnDefinition* col);

  static std::vector<ColumnPtr> convert_columns(const hsql::CreateStatement* create_stmt);

  static Table convert_create_statement(
      const hsql::CreateStatement* create_stmt,
      const std::unordered_map<std::string, std::vector<ColumnCheckConstraint>>& check_map);

 public:
  static std::vector<TablePtr> parse(const std::string& sql);

  static void foreign_key_resolver(std::vector<TablePtr>& tables);
};

}  // namespace schemaforge
