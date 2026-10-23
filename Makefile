.DEFAULT_GOAL := help

PYTHON ?= python3
PNPM ?= pnpm
HOST ?= 127.0.0.1
PORT ?= 8000
SCHEMAFORGE_BINARY ?= $(CURDIR)/build/schemaforge

.PHONY: help setup build native web run demo clean

help:
	@echo "SchemaForge local website"
	@echo "  make setup   Install Python and frontend dependencies (once)"
	@echo "  make build   Build the C++ engine and website"
	@echo "  make run     Start the website and API at http://$(HOST):$(PORT)"
	@echo "  make web     Rebuild only the website after frontend edits"
	@echo "  make native  Rebuild only the C++ engine after engine edits"
	@echo "  make demo    Run the original CLI sample"

setup:
	@if [ "$$(uname -s)" = Darwin ]; then \
		command -v brew >/dev/null || { echo "Install Homebrew before running make setup."; exit 1; }; \
		command -v cmake >/dev/null || brew install cmake; \
		brew list --versions yaml-cpp | grep -q . || brew install yaml-cpp; \
		command -v node >/dev/null || brew install node; \
		command -v $(PNPM) >/dev/null || brew install pnpm; \
	fi
	$(PYTHON) -m venv server/.venv
	server/.venv/bin/pip install -e 'server[test]'
	cd web && $(PNPM) install --frozen-lockfile

build: native web

native:
	@if [ "$$(uname -s)" = Darwin ] && [ -f external/sql-parser/libsqlparser.so ] && \
		file external/sql-parser/libsqlparser.so | grep -q ELF; then \
		$(MAKE) -C external/sql-parser clean; \
	fi
	cmake -S . -B build/native -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
	cmake --build build/native
	ln -sfn native/schemaforge build/schemaforge

web:
	cd web && $(PNPM) build

run:
	@test -x server/.venv/bin/uvicorn || { echo "Run 'make setup' first (Python server missing)."; exit 1; }
	@test -x "$(SCHEMAFORGE_BINARY)" || { echo "Run 'make build' first (C++ engine missing)."; exit 1; }
	@test -f web/dist/index.html || { echo "Run 'make build' first (website missing)."; exit 1; }
	@set -a; if [ -f server/.env.local ]; then . server/.env.local; fi; set +a; \
		SCHEMAFORGE_BINARY="$(SCHEMAFORGE_BINARY)" exec server/.venv/bin/uvicorn \
		schemaforge_api.app:app --app-dir server --host "$(HOST)" --port "$(PORT)"

demo:
	./build/schemaforge init --schema tests/valid/basic_fk/schema.sql --config schemaforge.yaml
	./build/schemaforge generate --config schemaforge.yaml

clean:
	rm -rf build
