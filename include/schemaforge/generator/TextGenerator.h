#pragma once
#include <string>
#include <vector>

#include "schemaforge/generator/GeneratedValue.h"

namespace schemaforge {

class TextGenerator {
 private:
  std::string column_name;

 public:
  TextGenerator();
  explicit TextGenerator(std::string column_name);
  std::vector<GeneratedValue> generate(int size);
};
}  // namespace schemaforge
