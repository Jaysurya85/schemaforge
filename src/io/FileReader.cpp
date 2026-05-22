
#include "schemaforge/io/FileReader.h"
#include <fstream>

namespace schemaforge {

std::string FileReader::read_file(const std::string &file_path) {
  std::ifstream file(file_path);
  std::string line;
  std::string sql;

  if (file.is_open()) {
    while (std::getline(file, line)) {
      sql += line + "\n";
    }
    file.close();
  } else {
    throw std::runtime_error("Could not open file: " + file_path);
  }
  return sql;
}
} // namespace schemaforge
