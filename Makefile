BUILD_DIR ?= build
INSTALL_PREFIX ?= /usr/local
CMAKE ?= cmake
CTEST ?= ctest

.PHONY: all configure build test install clean distclean clangd help

all: build

configure:
	$(CMAKE) -S . -B $(BUILD_DIR)

build: configure
	$(CMAKE) --build $(BUILD_DIR)

test: build
	$(CMAKE) -E chdir $(BUILD_DIR) $(CTEST) --output-on-failure

install: build
	$(CMAKE) --install $(BUILD_DIR) --prefix $(INSTALL_PREFIX)

clean:
	$(CMAKE) --build $(BUILD_DIR) --target clean

distclean:
	rm -rf $(BUILD_DIR) compile_commands.json

clangd: configure
	$(CMAKE) --build $(BUILD_DIR) --target clangd

help:
	@echo "Targets:"
	@echo "  make configure           Configure CMake into $(BUILD_DIR)"
	@echo "  make build               Build the shared library, tests, and examples"
	@echo "  make test                Build and run tests"
	@echo "  make install             Install to INSTALL_PREFIX, default /usr/local"
	@echo "  make clean               Clean CMake build outputs"
	@echo "  make distclean           Remove $(BUILD_DIR) and root compile_commands.json"
	@echo "  make clangd              Export compile_commands.json for clangd"
