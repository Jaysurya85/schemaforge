cmake_minimum_required(VERSION 3.20)

project(schemaforge VERSION 0.1.0 LANGUAGES CXX)

include(GNUInstallDirs)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(SQLite3 REQUIRED)
find_package(yaml-cpp REQUIRED)
find_program(MAKE_EXECUTABLE NAMES gmake make REQUIRED)

set(SCHEMAFORGE_SQLPARSER_DIR "${CMAKE_CURRENT_SOURCE_DIR}/external/sql-parser")
set(SCHEMAFORGE_SQLPARSER_LIBRARY "${SCHEMAFORGE_SQLPARSER_DIR}/libsqlparser.so")

add_custom_command(
    OUTPUT "${SCHEMAFORGE_SQLPARSER_LIBRARY}"
    COMMAND "${CMAKE_COMMAND}" -E env "CXX=${CMAKE_CXX_COMPILER}"
            "${MAKE_EXECUTABLE}" -C "${SCHEMAFORGE_SQLPARSER_DIR}"
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    COMMENT "Building bundled Hyrise SQL parser"
    VERBATIM
)

add_custom_target(schemaforge_sqlparser DEPENDS "${SCHEMAFORGE_SQLPARSER_LIBRARY}")

add_library(hyrise_sqlparser SHARED IMPORTED GLOBAL)
set_target_properties(hyrise_sqlparser PROPERTIES
    IMPORTED_LOCATION "${SCHEMAFORGE_SQLPARSER_LIBRARY}"
    IMPORTED_NO_SONAME TRUE
    INTERFACE_INCLUDE_DIRECTORIES "${SCHEMAFORGE_SQLPARSER_DIR}/src"
)
add_dependencies(hyrise_sqlparser schemaforge_sqlparser)

set(SCHEMAFORGE_SOURCES
    src/main.cpp
    src/benchmark/BenchmarkEngine.cpp
    src/config/GenerationConfig.cpp
    src/domain/ColumnDomainResolver.cpp
    src/generator/GenerationPlan.cpp
    src/generator/KeyRegistry.cpp
    src/generator/RealisticValueGenerator.cpp
    src/generator/ValueGenerator.cpp
    src/generator/BooleanGenerator.cpp
    src/generator/DecimalGenerator.cpp
    src/generator/IntGenerator.cpp
    src/generator/TextGenerator.cpp
    src/graph/DependencyGraph.cpp
    src/io/FileReader.cpp
    src/output/CsvWriter.cpp
    src/output/PostgresCopyWriter.cpp
    src/output/SqlInsertWriter.cpp
    src/parser/ParserAdapter.cpp
    src/schema/Column.cpp
    src/schema/Table.cpp
    src/validation/CapacityAnalyzer.cpp
    src/validation/PostgresDockerValidator.cpp
    src/validation/SQLiteValidator.cpp
    src/validation/ValidationResult.cpp
    src/validation/ValidationRunner.cpp
)

add_executable(schemaforge ${SCHEMAFORGE_SOURCES})
add_dependencies(schemaforge schemaforge_sqlparser)

target_include_directories(schemaforge PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

target_link_libraries(schemaforge PRIVATE
    hyrise_sqlparser
    SQLite3::SQLite3
    yaml-cpp::yaml-cpp
)

if(APPLE)
    set(SCHEMAFORGE_INSTALL_RPATH "@loader_path/../${CMAKE_INSTALL_LIBDIR}")
else()
    set(SCHEMAFORGE_INSTALL_RPATH "$ORIGIN/../${CMAKE_INSTALL_LIBDIR}")
endif()

set_target_properties(schemaforge PROPERTIES
    OUTPUT_NAME schemaforge
    BUILD_RPATH "${SCHEMAFORGE_SQLPARSER_DIR}"
    INSTALL_RPATH "${SCHEMAFORGE_INSTALL_RPATH}"
)

install(TARGETS schemaforge
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
)

install(FILES "${SCHEMAFORGE_SQLPARSER_LIBRARY}"
    DESTINATION "${CMAKE_INSTALL_LIBDIR}"
)
