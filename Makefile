BUILD_DIR ?= build
CMAKE ?= cmake
C_COMPILER ?= /usr/bin/gcc-12
CXX_COMPILER ?= /usr/bin/g++-12
STATIC_RUNTIME ?= ON
MAKEFLAGS += --no-print-directory

.PHONY: all install_deps configure build package clean distclean run

all: build

install_deps:
	sudo apt-get update
	sudo apt-get install -y gcc-12 g++-12 cmake make

configure:
	$(CMAKE) -S . -B $(BUILD_DIR) -DCMAKE_EXPORT_COMPILE_COMMANDS=ON -DFLIGHTLOGGER_STATIC_RUNTIME=$(STATIC_RUNTIME) -DCMAKE_C_COMPILER=$(C_COMPILER) -DCMAKE_CXX_COMPILER=$(CXX_COMPILER)

build: configure
	$(CMAKE) --build $(BUILD_DIR) -j

package: build
	$(CMAKE) --build $(BUILD_DIR) --target package

clean:
	$(CMAKE) --build $(BUILD_DIR) --target clean

distclean:
	@rm -rf $(BUILD_DIR)

run: build
	@./$(BUILD_DIR)/flight_logger_smoke
