#pragma once
#include <string>
#include <vector>

#include "schemaforge/generator/GeneratedValue.h"

namespace schemaforge {

class DecimalGenerator {
 private:
  std::string column_name;

 public:
  DecimalGenerator();
  explicit DecimalGenerator(std::string column_name);
  std::vector<GeneratedValue> generate(int size);
};
}  // namespace schemaforge
