#pragma once

#include <string>
#include <vector>

#include "../schema/Table.h"
#include "sql/ColumnType.h"
#include "sql/CreateStatement.h"

namespace schemaforge {

class ParserAdapter {
 private:
  static DataType convert_data_type(hsql::DataType data_type);

  static ColumnType convert_column_type(const hsql::ColumnType& hsql_column_type);

  static ConstraintType convert_constraint_type(const hsql::ConstraintType& constraint_type);

  static TableConstraint convert_table_constraint(const hsql::TableConstraint* table_constraint);

  static std::vector<TableConstraint> extract_table_constraints(
      const hsql::CreateStatement* create_stmt);

  [[nodiscard]] static bool should_store_as_table_constraint(TableConstraint& table_contraint);

  static std::vector<TableConstraint> extract_column_constraints(
      const hsql::CreateStatement* create_stmt);

  static std::vector<TableConstraint> convert_constraints(const hsql::CreateStatement* create_stmt);

  static ForeignKey convert_foreign_key(const hsql::ForeignKeyConstraint* foreign_key_constraint);

  static std::vector<ForeignKey> extract_table_foreign_keys(
      const hsql::CreateStatement* create_stmt);

  static std::vector<ForeignKey> extract_column_foreign_keys(
      const hsql::CreateStatement* create_stmt);

  static std::vector<ForeignKey> extract_foreign_keys(const hsql::CreateStatement* create_stmt);

  static std::vector<std::string> convert_names(const std::vector<char*>* names);

  static Column convert_column(const hsql::ColumnDefinition* col);

  static std::vector<Column> convert_columns(const hsql::CreateStatement* create_stmt);

  static Table convert_create_statement(const hsql::CreateStatement* create_stmt);

 public:
  static std::vector<Table> parse(const std::string& sql);
};

}  // namespace schemaforge
