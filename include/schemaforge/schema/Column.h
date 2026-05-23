#pragma once
#include <ostream>
#include <string>

namespace schemaforge {

enum class DataType {
  UNKNOWN,
  BIGINT,
  BOOLEAN,
  CHAR,
  DATE,
  DATETIME,
  DECIMAL,
  DOUBLE,
  FLOAT,
  INT,
  LONG,
  REAL,
  SMALLINT,
  TEXT,
  TIME,
  VARCHAR,
};

struct ColumnType {
  DataType data_type{DataType::UNKNOWN};
  int64_t length{0};     // Used for, e.g., VARCHAR(10)
  int64_t precision{0};  // Used for, e.g., DECIMAL (6, 4) or TIME (5)
  int64_t scale{0};      // Used for DECIMAL (6, 4)
};

class Column {
 private:
  std::string column_name;
  ColumnType column_type;
  bool nullable{true};

 public:
  Column(const std::string& column_name, ColumnType column_type, bool nullable = true);
  [[nodiscard]] std::string get_column_name() const;
  [[nodiscard]] ColumnType get_column_type() const;
  [[nodiscard]] bool is_nullable() const;
};
std::ostream& operator<<(std::ostream& os, const Column& column);
std::ostream& operator<<(std::ostream& os, const DataType& data_type);
}  // namespace schemaforge
