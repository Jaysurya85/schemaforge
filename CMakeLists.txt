cmake_minimum_required(VERSION 3.20)

project(schemaforge VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(SQLite3 REQUIRED)
find_package(yaml-cpp REQUIRED)

add_executable(schemaforge
    src/main.cpp
    src/benchmark/BenchmarkEngine.cpp
    src/parser/ParserAdapter.cpp
    src/schema/Table.cpp
    src/schema/Column.cpp
    src/validation/SchemaValidator.cpp
    src/validation/CapacityAnalyzer.cpp
    src/validation/GenerationFeasibilityValidator.cpp
    src/validation/SQLiteValidator.cpp
    src/graph/DependencyGraph.cpp
    src/config/GenerationConfig.cpp
    src/generator/GenerationPlan.cpp
    src/generator/KeyRegistry.cpp
    src/generator/ValueGenerator.cpp
    src/generator/BooleanGenerator.cpp
    src/generator/DecimalGenerator.cpp
    src/generator/IntGenerator.cpp
    src/generator/TextGenerator.cpp
    src/io/FileReader.cpp
    src/output/SqlInsertWriter.cpp
)

target_include_directories(schemaforge PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/external/sql-parser/src ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(schemaforge PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/external/sql-parser/libsqlparser.so
    SQLite3::SQLite3
    yaml-cpp::yaml-cpp
)
