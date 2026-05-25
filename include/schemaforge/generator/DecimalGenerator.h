#pragma once
#include <string>
#include <vector>

namespace schemaforge {
using Data = std::string;

class DecimalGenerator {
 private:
  std::string column_name;

 public:
  DecimalGenerator();
  explicit DecimalGenerator(std::string column_name);
  std::vector<Data> generate(int size);
};
}  // namespace schemaforge
