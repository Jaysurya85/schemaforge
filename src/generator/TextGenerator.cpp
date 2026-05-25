#include "schemaforge/generator/TextGenerator.h"

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

bool is_email_column(const std::string& column_name) {
  return column_name.find("email") != std::string::npos && !column_name.ends_with("_id") &&
         !column_name.ends_with("id");
}

}  // namespace

TextGenerator::TextGenerator() : column_name("text") {}

TextGenerator::TextGenerator(std::string column_name)
    : column_name(normalize_column_name(std::move(column_name))) {}

std::vector<Data> TextGenerator::generate(int size) {
  std::vector<Data> result;
  result.reserve(size);
  for (int row = 1; row <= size; ++row) {
    if (is_email_column(column_name)) {
      result.push_back(column_name + "_" + std::to_string(row) + "@example.com");
      continue;
    }
    result.push_back(column_name + "_" + std::to_string(row));
  }
  return result;
};

}  // namespace schemaforge
