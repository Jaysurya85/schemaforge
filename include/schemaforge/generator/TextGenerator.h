#pragma once
#include <string>
#include <vector>

namespace schemaforge {
using Data = std::string;

class TextGenerator {
 private:
  std::string column_name;

 public:
  TextGenerator();
  explicit TextGenerator(std::string column_name);
  std::vector<Data> generate(int size);
};
}  // namespace schemaforge
