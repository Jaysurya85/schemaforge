#include "schemaforge/generator/DecimalGenerator.h"

#include <algorithm>
#include <cctype>
#include <utility>

namespace schemaforge {

namespace {

std::string normalize_column_name(std::string column_name) {
  std::ranges::transform(column_name, column_name.begin(), [](unsigned char character) {
    if (std::isalnum(character) != 0) {
      return static_cast<char>(std::tolower(character));
    }
    return '_';
  });
  return column_name;
}

}  // namespace

DecimalGenerator::DecimalGenerator() : column_name("decimal") {}

DecimalGenerator::DecimalGenerator(std::string column_name)
    : column_name(normalize_column_name(std::move(column_name))) {}

std::vector<GeneratedValue> DecimalGenerator::generate(int size) {
  std::vector<GeneratedValue> result;
  result.reserve(size);

  for (int row = 1; row <= size; ++row) {
    double value = static_cast<double>(row) + 0.5;
    if (column_name.find("amount") != std::string::npos) {
      value = static_cast<double>(row * 10) + 0.5;
    } else if (column_name.find("price") != std::string::npos) {
      value = static_cast<double>(row) + 0.99;
    }

    result.push_back(GeneratedValue::numeric(value));
  }

  return result;
}

}  // namespace schemaforge
