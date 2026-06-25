#include "schemaforge/parser/ParserAdapter.h"

#include <algorithm>
#include <cctype>
#include <optional>
#include <regex>
#include <sstream>
#include <unordered_map>

#include "SQLParser.h"

namespace schemaforge {

DataType ParserAdapter::convert_data_type(hsql::DataType data_type) {
  switch (data_type) {
    case hsql::DataType::BIGINT:
      return DataType::BIGINT;
    case hsql::DataType::BOOLEAN:
      return DataType::BOOLEAN;
    case hsql::DataType::CHAR:
      return DataType::CHAR;
    case hsql::DataType::DATE:
      return DataType::DATE;
    case hsql::DataType::DATETIME:
      return DataType::DATETIME;
    case hsql::DataType::DECIMAL:
      return DataType::DECIMAL;
    case hsql::DataType::DOUBLE:
      return DataType::DOUBLE;
    case hsql::DataType::FLOAT:
      return DataType::FLOAT;
    case hsql::DataType::INT:
      return DataType::INT;
    case hsql::DataType::LONG:
      return DataType::LONG;
    case hsql::DataType::REAL:
      return DataType::REAL;
    case hsql::DataType::SMALLINT:
      return DataType::SMALLINT;
    case hsql::DataType::TEXT:
      return DataType::TEXT;
    case hsql::DataType::TIME:
      return DataType::TIME;
    case hsql::DataType::VARCHAR:
      return DataType::VARCHAR;
    default:
      return DataType::UNKNOWN;
  }
}

ColumnType ParserAdapter::convert_column_type(const hsql::ColumnType& hsql_column_type) {
  return ColumnType{.data_type = convert_data_type(hsql_column_type.data_type),
                    .length = hsql_column_type.length,
                    .precision = hsql_column_type.precision,
                    .scale = hsql_column_type.scale};
}

ConstraintType ParserAdapter::convert_constraint_type(const hsql::ConstraintType& constraint_type) {
  switch (constraint_type) {
    case hsql::ConstraintType::PrimaryKey:
      return ConstraintType::PrimaryKey;
    case hsql::ConstraintType::ForeignKey:
      return ConstraintType::ForeignKey;
    case hsql::ConstraintType::NotNull:
      return ConstraintType::NotNull;
    case hsql::ConstraintType::Null:
      return ConstraintType::Null;
    case hsql::ConstraintType::Unique:
      return ConstraintType::Unique;
    default:
      return ConstraintType::Unknown;
  }
}

Column* ParserAdapter::find_column_by_name(const std::string& name,
                                           const std::vector<ColumnPtr>& columns) {
  for (const auto& column : columns) {
    if (column->get_column_name() == name) {
      return column.get();
    }
  }
  return nullptr;
}

Table* ParserAdapter::find_table_by_name(const std::string& name,
                                         const std::vector<TablePtr>& tables) {
  for (const auto& table : tables) {
    if (table->get_table_name() == name) {
      return table.get();
    }
  }
  return nullptr;
}

namespace {

struct CheckPreprocessResult {
  std::string sql_without_checks;
  std::unordered_map<std::string, std::vector<ColumnCheckConstraint>> checks_by_table;
};

bool is_word_character(char character) {
  return std::isalnum(static_cast<unsigned char>(character)) != 0 || character == '_';
}

std::string trim(const std::string& value) {
  const auto begin = std::ranges::find_if(value, [](unsigned char character) {
    return std::isspace(character) == 0;
  });
  if (begin == value.end()) {
    return "";
  }

  const auto end = std::find_if(value.rbegin(), value.rend(), [](unsigned char character) {
    return std::isspace(character) == 0;
  }).base();
  return std::string(begin, end);
}

std::string uppercase(std::string value) {
  std::ranges::transform(value, value.begin(), [](unsigned char character) {
    return static_cast<char>(std::toupper(character));
  });
  return value;
}

std::string first_word(const std::string& value) {
  std::istringstream input(value);
  std::string word;
  input >> word;
  return word;
}

std::vector<std::string> split_top_level(const std::string& value, char delimiter) {
  std::vector<std::string> parts;
  std::string current;
  int parenthesis_depth = 0;
  bool in_string = false;

  for (std::size_t index = 0; index < value.size(); ++index) {
    const char character = value[index];
    if (character == '\'') {
      in_string = !in_string;
    }

    if (!in_string) {
      if (character == '(') {
        parenthesis_depth++;
      } else if (character == ')' && parenthesis_depth > 0) {
        parenthesis_depth--;
      }
    }

    if (character == delimiter && parenthesis_depth == 0 && !in_string) {
      parts.push_back(trim(current));
      current.clear();
      continue;
    }

    current += character;
  }

  const std::string final_part = trim(current);
  if (!final_part.empty()) {
    parts.push_back(final_part);
  }

  return parts;
}

bool keyword_at(const std::string& value, std::size_t position, const std::string& keyword) {
  if (position + keyword.size() > value.size()) {
    return false;
  }
  for (std::size_t index = 0; index < keyword.size(); ++index) {
    if (std::toupper(static_cast<unsigned char>(value[position + index])) !=
        std::toupper(static_cast<unsigned char>(keyword[index]))) {
      return false;
    }
  }
  const bool starts_word = position == 0 || !is_word_character(value[position - 1]);
  const std::size_t after_keyword = position + keyword.size();
  const bool ends_word = after_keyword >= value.size() || !is_word_character(value[after_keyword]);
  return starts_word && ends_word;
}

bool contains_top_level_keyword(const std::string& value, const std::string& keyword) {
  int parenthesis_depth = 0;
  bool in_string = false;
  for (std::size_t index = 0; index < value.size(); ++index) {
    const char character = value[index];
    if (character == '\'') {
      in_string = !in_string;
    }
    if (in_string) {
      continue;
    }
    if (character == '(') {
      parenthesis_depth++;
      continue;
    }
    if (character == ')' && parenthesis_depth > 0) {
      parenthesis_depth--;
      continue;
    }
    if (parenthesis_depth == 0 && keyword_at(value, index, keyword)) {
      return true;
    }
  }
  return false;
}

std::vector<std::string> split_top_level_and(const std::string& value) {
  std::vector<std::string> parts;
  std::string current;
  int parenthesis_depth = 0;
  bool in_string = false;
  bool waiting_for_between_and = false;

  for (std::size_t index = 0; index < value.size();) {
    const char character = value[index];
    if (character == '\'') {
      in_string = !in_string;
      current += character;
      index++;
      continue;
    }

    if (!in_string) {
      if (character == '(') {
        parenthesis_depth++;
      } else if (character == ')' && parenthesis_depth > 0) {
        parenthesis_depth--;
      }

      if (parenthesis_depth == 0 && keyword_at(value, index, "BETWEEN")) {
        waiting_for_between_and = true;
      }

      if (parenthesis_depth == 0 && keyword_at(value, index, "AND")) {
        if (waiting_for_between_and) {
          waiting_for_between_and = false;
        } else {
          parts.push_back(trim(current));
          current.clear();
          index += 3;
          continue;
        }
      }
    }

    current += character;
    index++;
  }

  const std::string final_part = trim(current);
  if (!final_part.empty()) {
    parts.push_back(final_part);
  }
  return parts;
}

std::optional<double> parse_number(const std::string& value) {
  try {
    std::size_t parsed = 0;
    const double number = std::stod(value, &parsed);
    if (parsed != value.size()) {
      return std::nullopt;
    }
    return number;
  } catch (...) {
    return std::nullopt;
  }
}

std::string unquote_sql_string(const std::string& value) {
  std::string trimmed = trim(value);
  if (trimmed.size() < 2 || trimmed.front() != '\'' || trimmed.back() != '\'') {
    return trimmed;
  }

  trimmed = trimmed.substr(1, trimmed.size() - 2);
  std::string unescaped;
  unescaped.reserve(trimmed.size());
  for (std::size_t index = 0; index < trimmed.size(); ++index) {
    if (trimmed[index] == '\'' && index + 1 < trimmed.size() && trimmed[index + 1] == '\'') {
      unescaped += '\'';
      index++;
      continue;
    }
    unescaped += trimmed[index];
  }
  return unescaped;
}

std::string first_identifier(const std::string& expression) {
  const std::regex identifier_pattern(R"(([A-Za-z_][A-Za-z0-9_]*))");
  std::smatch match;
  if (std::regex_search(expression, match, identifier_pattern)) {
    return match[1].str();
  }
  return "";
}

std::optional<CheckComparisonOperator> parse_comparison_operator(const std::string& op) {
  if (op == "<") {
    return CheckComparisonOperator::LessThan;
  }
  if (op == "<=") {
    return CheckComparisonOperator::LessEqual;
  }
  if (op == ">") {
    return CheckComparisonOperator::GreaterThan;
  }
  if (op == ">=") {
    return CheckComparisonOperator::GreaterEqual;
  }
  if (op == "=") {
    return CheckComparisonOperator::Equal;
  }
  return std::nullopt;
}

ColumnCheckConstraint parse_single_check_expression(const std::string& expression,
                                                    const std::string& raw_sql) {
  ColumnCheckConstraint check;
  check.expression = trim(expression);
  check.raw_sql = raw_sql;
  check.column_name = first_identifier(check.expression);

  std::smatch match;
  const std::regex between_pattern(
      R"(^\s*([A-Za-z_][A-Za-z0-9_]*)\s+BETWEEN\s+([+-]?[0-9]+(?:\.[0-9]+)?)\s+AND\s+([+-]?[0-9]+(?:\.[0-9]+)?)\s*$)",
      std::regex_constants::icase);
  if (std::regex_match(check.expression, match, between_pattern)) {
    check.type = CheckConstraintType::Range;
    check.column_name = match[1].str();
    check.min_value = std::stod(match[2].str());
    check.max_value = std::stod(match[3].str());
    check.min_inclusive = true;
    check.max_inclusive = true;
    return check;
  }

  const std::regex comparison_pattern(
      R"(^\s*([A-Za-z_][A-Za-z0-9_]*)\s*(>=|<=|>|<)\s*([+-]?[0-9]+(?:\.[0-9]+)?)\s*$)",
      std::regex_constants::icase);
  if (std::regex_match(check.expression, match, comparison_pattern)) {
    check.type = CheckConstraintType::Range;
    check.column_name = match[1].str();
    const std::string op = match[2].str();
    const double number = std::stod(match[3].str());
    if (op == ">=" || op == ">") {
      check.min_value = number;
      check.min_inclusive = op == ">=";
    } else {
      check.max_value = number;
      check.max_inclusive = op == "<=";
    }
    return check;
  }

  const std::regex in_pattern(
      R"(^\s*([A-Za-z_][A-Za-z0-9_]*)\s+IN\s*\((.*)\)\s*$)",
      std::regex_constants::icase);
  if (std::regex_match(check.expression, match, in_pattern)) {
    check.type = CheckConstraintType::AllowedValues;
    check.column_name = match[1].str();
    for (const auto& value : split_top_level(match[2].str(), ',')) {
      const std::string trimmed_value = trim(value);
      if (!trimmed_value.empty() && trimmed_value.front() == '\'') {
        check.allowed_values.push_back(GeneratedValue::text(unquote_sql_string(trimmed_value)));
        continue;
      }

      const auto numeric_value = parse_number(trimmed_value);
      if (!numeric_value.has_value()) {
        check.type = CheckConstraintType::Unsupported;
        check.allowed_values.clear();
        return check;
      }
      check.allowed_values.push_back(GeneratedValue::numeric(numeric_value.value()));
    }
    if (check.allowed_values.empty()) {
      check.type = CheckConstraintType::Unsupported;
    }
    return check;
  }

  const std::regex boolean_equality_pattern(
      R"(^\s*([A-Za-z_][A-Za-z0-9_]*)\s*=\s*(true|false)\s*$)",
      std::regex_constants::icase);
  if (std::regex_match(check.expression, match, boolean_equality_pattern)) {
    check.type = CheckConstraintType::AllowedValues;
    check.column_name = match[1].str();
    check.allowed_values.push_back(
        GeneratedValue::boolean(uppercase(match[2].str()) == "TRUE"));
    return check;
  }

  const std::regex column_comparison_pattern(
      R"(^\s*([A-Za-z_][A-Za-z0-9_]*)\s*(>=|<=|=|>|<)\s*([A-Za-z_][A-Za-z0-9_]*)\s*$)",
      std::regex_constants::icase);
  if (std::regex_match(check.expression, match, column_comparison_pattern)) {
    const auto comparison_operator = parse_comparison_operator(match[2].str());
    if (comparison_operator.has_value()) {
      check.type = CheckConstraintType::ColumnComparison;
      check.column_name = match[1].str();
      check.right_column_name = match[3].str();
      check.comparison_operator = comparison_operator.value();
      return check;
    }
  }

  return check;
}

std::vector<ColumnCheckConstraint> parse_check_expression(const std::string& expression,
                                                         const std::string& raw_sql) {
  const std::string trimmed_expression = trim(expression);
  if (contains_top_level_keyword(trimmed_expression, "OR")) {
    ColumnCheckConstraint check;
    check.expression = trimmed_expression;
    check.raw_sql = raw_sql;
    check.column_name = first_identifier(trimmed_expression);
    return {check};
  }

  const auto parts = split_top_level_and(trimmed_expression);
  if (parts.size() <= 1) {
    return {parse_single_check_expression(trimmed_expression, raw_sql)};
  }

  std::vector<ColumnCheckConstraint> checks;
  checks.reserve(parts.size());
  for (const auto& part : parts) {
    checks.push_back(parse_single_check_expression(part, raw_sql));
  }
  return checks;
}

std::size_t find_keyword(const std::string& value, const std::string& keyword, std::size_t start) {
  for (std::size_t position = start; position + keyword.size() <= value.size(); ++position) {
    bool matches = true;
    for (std::size_t index = 0; index < keyword.size(); ++index) {
      if (std::toupper(static_cast<unsigned char>(value[position + index])) !=
          std::toupper(static_cast<unsigned char>(keyword[index]))) {
        matches = false;
        break;
      }
    }
    if (!matches) {
      continue;
    }

    const bool starts_word = position == 0 || !is_word_character(value[position - 1]);
    const std::size_t after_keyword = position + keyword.size();
    const bool ends_word = after_keyword >= value.size() || !is_word_character(value[after_keyword]);
    if (starts_word && ends_word) {
      return position;
    }
  }
  return std::string::npos;
}

std::optional<std::pair<std::string, std::size_t>> extract_check_clause(const std::string& value,
                                                                        std::size_t start) {
  std::size_t check_position = find_keyword(value, "CHECK", start);
  while (check_position != std::string::npos) {
    const std::size_t after_check = check_position + 5;
    std::size_t open_paren = after_check;
    while (open_paren < value.size() && std::isspace(static_cast<unsigned char>(value[open_paren])) != 0) {
      open_paren++;
    }
    if (open_paren >= value.size() || value[open_paren] != '(') {
      return std::nullopt;
    }

    int depth = 0;
    bool in_string = false;
    for (std::size_t index = open_paren; index < value.size(); ++index) {
      if (value[index] == '\'') {
        in_string = !in_string;
      }
      if (in_string) {
        continue;
      }
      if (value[index] == '(') {
        depth++;
      } else if (value[index] == ')') {
        depth--;
        if (depth == 0) {
          const std::string expression = value.substr(open_paren + 1, index - open_paren - 1);
          return std::make_pair(expression, index + 1);
        }
      }
    }
    return std::nullopt;
  }

  return std::nullopt;
}

bool case_insensitive_char_match(char left, char right) {
  return std::toupper(static_cast<unsigned char>(left)) ==
         std::toupper(static_cast<unsigned char>(right));
}

bool case_insensitive_match_at(const std::string& value, std::size_t position,
                               const std::string& target) {
  if (position + target.size() > value.size()) {
    return false;
  }

  for (std::size_t index = 0; index < target.size(); ++index) {
    if (!case_insensitive_char_match(value[position + index], target[index])) {
      return false;
    }
  }
  return true;
}

std::string normalize_bare_char_type(const std::string& sql) {
  std::string normalized;
  normalized.reserve(sql.size());

  for (std::size_t index = 0; index < sql.size();) {
    const bool matches_char = case_insensitive_match_at(sql, index, "CHAR");
    const bool starts_word = index == 0 || !is_word_character(sql[index - 1]);
    const std::size_t after_char = index + 4;
    const bool ends_word = after_char >= sql.size() || !is_word_character(sql[after_char]);

    if (!matches_char || !starts_word || !ends_word) {
      normalized += sql[index++];
      continue;
    }

    std::size_t next = after_char;
    while (next < sql.size() && std::isspace(static_cast<unsigned char>(sql[next])) != 0) {
      next++;
    }

    if (next < sql.size() && sql[next] == '(') {
      normalized.append(sql, index, after_char - index);
      index = after_char;
      continue;
    }

    normalized += "CHAR(1)";
    index = after_char;
  }

  return normalized;
}

std::string strip_inline_checks(const std::string& definition, const std::string& table_name,
                                const std::string& column_name,
                                std::vector<ColumnCheckConstraint>& checks) {
  std::string stripped;
  std::size_t cursor = 0;
  while (cursor < definition.size()) {
    auto check_clause = extract_check_clause(definition, cursor);
    if (!check_clause.has_value()) {
      stripped += definition.substr(cursor);
      break;
    }

    const std::size_t check_position = find_keyword(definition, "CHECK", cursor);
    stripped += definition.substr(cursor, check_position - cursor);
    const std::string raw_sql =
        definition.substr(check_position, check_clause->second - check_position);
    for (auto check : parse_check_expression(check_clause->first, raw_sql)) {
      if (check.column_name.empty()) {
        check.column_name = column_name;
      }
      checks.push_back(std::move(check));
    }
    cursor = check_clause->second;
  }

  return trim(stripped);
}

std::string remove_check_constraints_from_table_body(
    const std::string& table_name, const std::string& table_body,
    std::vector<ColumnCheckConstraint>& checks) {
  std::vector<std::string> kept_definitions;
  for (const auto& definition : split_top_level(table_body, ',')) {
    const std::string trimmed_definition = trim(definition);
    const std::string leading_word = uppercase(first_word(trimmed_definition));

    if (leading_word == "CHECK") {
      auto check_clause = extract_check_clause(trimmed_definition, 0);
      if (check_clause.has_value()) {
        for (auto check : parse_check_expression(check_clause->first, trimmed_definition)) {
          checks.push_back(std::move(check));
        }
      }
      continue;
    }

    if (leading_word == "CONSTRAINT") {
      const std::size_t check_position = uppercase(trimmed_definition).find("CHECK");
      if (check_position != std::string::npos) {
        auto check_clause = extract_check_clause(trimmed_definition, check_position);
        if (check_clause.has_value()) {
          const std::string raw_sql =
              trimmed_definition.substr(check_position, check_clause->second - check_position);
          for (auto check : parse_check_expression(check_clause->first, raw_sql)) {
            checks.push_back(std::move(check));
          }
        }
        continue;
      }
    }

    std::string stripped_definition =
        strip_inline_checks(trimmed_definition, table_name, first_word(trimmed_definition), checks);
    if (!stripped_definition.empty()) {
      kept_definitions.push_back(std::move(stripped_definition));
    }
  }

  std::string body;
  for (std::size_t index = 0; index < kept_definitions.size(); ++index) {
    if (index > 0) {
      body += ", ";
    }
    body += kept_definitions[index];
  }
  return body;
}

CheckPreprocessResult preprocess_check_constraints(const std::string& sql) {
  CheckPreprocessResult result{.sql_without_checks = "", .checks_by_table = {}};
  std::size_t cursor = 0;
  while (cursor < sql.size()) {
    const std::size_t create_position = uppercase(sql.substr(cursor)).find("CREATE TABLE");
    if (create_position == std::string::npos) {
      result.sql_without_checks += sql.substr(cursor);
      break;
    }

    const std::size_t statement_start = cursor + create_position;
    result.sql_without_checks += sql.substr(cursor, statement_start - cursor);

    const std::size_t open_paren = sql.find('(', statement_start);
    if (open_paren == std::string::npos) {
      result.sql_without_checks += sql.substr(statement_start);
      break;
    }

    std::istringstream header_stream(sql.substr(statement_start, open_paren - statement_start));
    std::string create_word;
    std::string table_word;
    std::string table_name;
    header_stream >> create_word >> table_word >> table_name;

    int depth = 0;
    bool in_string = false;
    std::size_t close_paren = std::string::npos;
    for (std::size_t index = open_paren; index < sql.size(); ++index) {
      if (sql[index] == '\'') {
        in_string = !in_string;
      }
      if (in_string) {
        continue;
      }
      if (sql[index] == '(') {
        depth++;
      } else if (sql[index] == ')') {
        depth--;
        if (depth == 0) {
          close_paren = index;
          break;
        }
      }
    }

    if (close_paren == std::string::npos) {
      result.sql_without_checks += sql.substr(statement_start);
      break;
    }

    const std::string table_body = sql.substr(open_paren + 1, close_paren - open_paren - 1);
    auto& checks = result.checks_by_table[table_name];
    const std::string stripped_body =
        remove_check_constraints_from_table_body(table_name, table_body, checks);

    result.sql_without_checks += sql.substr(statement_start, open_paren - statement_start + 1);
    result.sql_without_checks += stripped_body;
    result.sql_without_checks += ")";
    cursor = close_paren + 1;
  }

  return result;
}

std::vector<Column*> primary_key_columns(const Table* table) {
  for (const auto& constraint : table->get_table_constraints()) {
    if (constraint.type == ConstraintType::PrimaryKey) {
      return constraint.columns;
    }
  }
  return {};
}

}  // namespace

TableConstraint ParserAdapter::convert_table_constraint(
    const hsql::TableConstraint* table_constraint, const std::vector<ColumnPtr>& columns) {
  std::vector<Column*> contraint_columns;

  if (table_constraint != nullptr && table_constraint->columnNames != nullptr) {
    for (const auto* column_name : *table_constraint->columnNames) {
      if (column_name != nullptr) {
        contraint_columns.push_back(find_column_by_name(column_name, columns));
      }
    }
  }

  return {convert_constraint_type(table_constraint->type), contraint_columns};
}

bool ParserAdapter::should_store_as_table_constraint(TableConstraint& table_contraint) {
  if (table_contraint.columns.empty()) {
    return false;
  }

  switch (table_contraint.type) {
    case ConstraintType::PrimaryKey:
    case ConstraintType::Unique:
      return true;

    case ConstraintType::Null:
    case ConstraintType::NotNull:
    case ConstraintType::ForeignKey:
    case ConstraintType::Unknown:
    default:
      return false;
  }
}

std::vector<TableConstraint> ParserAdapter::extract_table_constraints(
    const hsql::CreateStatement* create_stmt, const std::vector<ColumnPtr>& columns) {
  std::vector<TableConstraint> table_constraints;

  if (create_stmt == nullptr || create_stmt->tableConstraints == nullptr) {
    return table_constraints;
  }

  for (const auto* constraint : *create_stmt->tableConstraints) {
    TableConstraint table_constraint = convert_table_constraint(constraint, columns);
    if (!should_store_as_table_constraint(table_constraint)) {
      continue;
    }
    table_constraints.push_back(convert_table_constraint(constraint, columns));
  }

  return table_constraints;
}

std::vector<TableConstraint> ParserAdapter::extract_column_constraints(
    const hsql::CreateStatement* create_stmt, const std::vector<ColumnPtr>& columns) {
  std::vector<TableConstraint> column_constraints;

  if (create_stmt == nullptr || create_stmt->columns == nullptr) {
    return column_constraints;
  }

  for (const auto* col : *create_stmt->columns) {
    if (col == nullptr || col->column_constraints == nullptr) {
      continue;
    }

    for (const auto& constraint : *col->column_constraints) {
      TableConstraint column_constraint(
          convert_constraint_type(constraint),
          std::vector<Column*>{find_column_by_name(col->name, columns)});

      if (!should_store_as_table_constraint(column_constraint)) {
        continue;
      }
      column_constraints.push_back(std::move(column_constraint));
    }
  }

  return column_constraints;
}

std::vector<TableConstraint> ParserAdapter::convert_constraints(
    const hsql::CreateStatement* create_stmt, const std::vector<ColumnPtr>& columns) {
  auto constraints = extract_table_constraints(create_stmt, columns);
  auto column_constraints = extract_column_constraints(create_stmt, columns);

  constraints.insert(constraints.end(), std::make_move_iterator(column_constraints.begin()),
                     std::make_move_iterator(column_constraints.end()));

  return constraints;
}

std::vector<std::string> ParserAdapter::convert_names(const std::vector<char*>* names) {
  std::vector<std::string> result;

  if (names == nullptr) {
    return result;
  }

  result.reserve(names->size());

  for (const auto* name : *names) {
    if (name != nullptr) {
      result.emplace_back(name);
    }
  }

  return result;
}

ForeignKeySpec ParserAdapter::convert_foreign_key_spec(
    const hsql::ForeignKeyConstraint* foreign_key_constraint) {
  std::vector<std::string> local_columns;
  std::string referenced_table;
  std::vector<std::string> referenced_columns;

  if (foreign_key_constraint != nullptr) {
    local_columns = convert_names(foreign_key_constraint->columnNames);
    const auto* references = foreign_key_constraint->references;

    if (references != nullptr) {
      if (references->table != nullptr) {
        referenced_table = references->table;
      }

      referenced_columns = convert_names(references->columns);
    }
  }

  return {local_columns, referenced_table, referenced_columns};
}

std::vector<ForeignKeySpec> ParserAdapter::extract_table_foreign_keys_spec(
    const hsql::CreateStatement* create_stmt) {
  std::vector<ForeignKeySpec> foreign_keys_spec;

  if (create_stmt == nullptr || create_stmt->tableConstraints == nullptr) {
    return foreign_keys_spec;
  }

  for (const auto* constraint : *create_stmt->tableConstraints) {
    if (constraint == nullptr || constraint->type != hsql::ConstraintType::ForeignKey) {
      continue;
    }

    const auto* foreign_key_constraint = static_cast<const hsql::ForeignKeyConstraint*>(constraint);

    foreign_keys_spec.push_back(convert_foreign_key_spec(foreign_key_constraint));
  }
  return foreign_keys_spec;
}

std::vector<ForeignKeySpec> ParserAdapter::extract_column_foreign_keys_spec(
    const hsql::CreateStatement* create_stmt) {
  std::vector<ForeignKeySpec> foreign_keys_spec;

  if (create_stmt == nullptr || create_stmt->columns == nullptr) {
    return foreign_keys_spec;
  }

  for (const auto* col : *create_stmt->columns) {
    if (col == nullptr || col->references == nullptr) {
      continue;
    }

    for (const auto* references : *col->references) {
      if (references == nullptr) {
        continue;
      }

      std::vector<std::string> local_columns{col->name};

      std::string referenced_table;
      if (references->table != nullptr) {
        referenced_table = references->table;
      }

      auto referenced_columns = convert_names(references->columns);

      foreign_keys_spec.emplace_back(local_columns, referenced_table, referenced_columns);
    }
  }
  return foreign_keys_spec;
}

std::vector<ForeignKeySpec> ParserAdapter::extract_foreign_keys_spec(
    const hsql::CreateStatement* create_stmt) {
  auto foreign_keys_spec = extract_table_foreign_keys_spec(create_stmt);
  auto column_foreign_keys_spec = extract_column_foreign_keys_spec(create_stmt);

  foreign_keys_spec.insert(foreign_keys_spec.end(),
                           std::make_move_iterator(column_foreign_keys_spec.begin()),
                           std::make_move_iterator(column_foreign_keys_spec.end()));

  return foreign_keys_spec;
}

Column ParserAdapter::convert_column(const hsql::ColumnDefinition* col) {
  ColumnType column_type{convert_column_type(col->type)};
  return {col->name, column_type, col->nullable};
}

std::vector<ColumnPtr> ParserAdapter::convert_columns(const hsql::CreateStatement* create_stmt) {
  std::vector<ColumnPtr> columns;

  if (create_stmt == nullptr || create_stmt->columns == nullptr) {
    return columns;
  }

  for (const auto* col : *create_stmt->columns) {
    if (col == nullptr) {
      continue;
    }

    columns.push_back(std::make_unique<Column>(convert_column(col)));
  }

  return columns;
}

Table ParserAdapter::convert_create_statement(
    const hsql::CreateStatement* create_stmt,
    const std::unordered_map<std::string, std::vector<ColumnCheckConstraint>>& check_map) {
  auto columns = convert_columns(create_stmt);
  auto constraints = convert_constraints(create_stmt, columns);
  auto foreign_keys_spec = extract_foreign_keys_spec(create_stmt);
  std::vector<ColumnCheckConstraint> checks;
  const auto checks_it = check_map.find(create_stmt->tableName);
  if (checks_it != check_map.end()) {
    checks = checks_it->second;
    for (auto& check : checks) {
      check.column = find_column_by_name(check.column_name, columns);
      if (!check.right_column_name.empty()) {
        check.right_column = find_column_by_name(check.right_column_name, columns);
      }
    }
  }

  return {create_stmt->tableName, std::move(columns), constraints, foreign_keys_spec, {},
          std::move(checks)};
}

std::vector<TablePtr> ParserAdapter::parse(const std::string& sql) {
  std::vector<TablePtr> tables;

  const CheckPreprocessResult check_preprocess_result = preprocess_check_constraints(sql);
  const std::string normalized_sql = normalize_bare_char_type(check_preprocess_result.sql_without_checks);
  hsql::SQLParserResult result;
  hsql::SQLParser::parse(normalized_sql, &result);

  auto statements = result.getStatements();

  for (const auto* stmt : statements) {
    if (stmt == nullptr || !stmt->isType(hsql::StatementType::kStmtCreate)) {
      continue;
    }

    const auto* create_stmt = static_cast<const hsql::CreateStatement*>(stmt);

    tables.push_back(std::make_unique<Table>(
        convert_create_statement(create_stmt, check_preprocess_result.checks_by_table)));
  }

  return tables;
}

void ParserAdapter::foreign_key_resolver(std::vector<TablePtr>& tables) {
  for (auto& table_ptr : tables) {
    std::vector<ForeignKey> foreign_keys;
    Table* table = table_ptr.get();
    for (auto& foreign_key_spec : table->get_foreign_key_specs()) {
      Table* referenced_table = find_table_by_name(foreign_key_spec.referenced_table, tables);
      std::vector<Column*> local_columns_ptrs;
      std::vector<Column*> referenced_columns_ptrs;
      local_columns_ptrs.reserve(foreign_key_spec.local_columns.size());
      referenced_columns_ptrs.reserve(foreign_key_spec.referenced_columns.size());

      for (const auto& local_column : foreign_key_spec.local_columns) {
        local_columns_ptrs.push_back(find_column_by_name(local_column, table->get_columns()));
      }

      for (const auto& referenced_column : foreign_key_spec.referenced_columns) {
        referenced_columns_ptrs.push_back(
            find_column_by_name(referenced_column, referenced_table->get_columns()));
      }
      if (referenced_columns_ptrs.empty()) {
        referenced_columns_ptrs = primary_key_columns(referenced_table);
      }
      foreign_keys.emplace_back(local_columns_ptrs, referenced_table, referenced_columns_ptrs);
    }
    table->set_foreign_keys(foreign_keys);
  }
}

}  // namespace schemaforge
