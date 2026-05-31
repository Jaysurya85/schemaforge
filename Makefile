all: clean build run


clean:
	rm -rf build
build:
	cmake -S . -B build -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
	cmake --build build

run: 
	./build/schemaforge init --schema schema.sql --config schemaforge.yaml
	./build/schemaforge generate --config schemaforge.yaml
