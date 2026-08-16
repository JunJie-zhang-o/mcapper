BUILD_DIR ?= build
CMAKE ?= cmake
C_COMPILER ?= /usr/bin/gcc-12
CXX_COMPILER ?= /usr/bin/g++-12
STATIC_RUNTIME ?= ON
BUILD_EXAMPLES ?= ON
BUILD_TESTS ?= ON
ENABLE_ROS1 ?= OFF
ENABLE_ROS2 ?= OFF
TARGET ?=
BUILD_TARGET_ARG := $(if $(TARGET),--target $(TARGET),)
MAKEFLAGS += --no-print-directory

.PHONY: all install_deps configure build package clean distclean run ros1_dynamic_recorder

all: build

install_deps:
	sudo apt-get update
	sudo apt-get install -y gcc-12 g++-12 cmake make

configure:
	$(CMAKE) -S . -B $(BUILD_DIR) -DCMAKE_EXPORT_COMPILE_COMMANDS=ON -DFLIGHTLOGGER_STATIC_RUNTIME=$(STATIC_RUNTIME) -DFLIGHTLOGGER_BUILD_EXAMPLES=$(BUILD_EXAMPLES) -DFLIGHTLOGGER_BUILD_TESTS=$(BUILD_TESTS) -DFLIGHTLOGGER_ENABLE_ROS1=$(ENABLE_ROS1) -DFLIGHTLOGGER_ENABLE_ROS2=$(ENABLE_ROS2) -DCMAKE_C_COMPILER=$(C_COMPILER) -DCMAKE_CXX_COMPILER=$(CXX_COMPILER)

build: configure
	$(CMAKE) --build $(BUILD_DIR) -j $(BUILD_TARGET_ARG)

ros1_dynamic_recorder:
	$(MAKE) build BUILD_DIR=build-ros1 ENABLE_ROS1=ON BUILD_EXAMPLES=ON TARGET=ros1_dynamic_recorder

package: build
	$(CMAKE) --build $(BUILD_DIR) --target package

clean:
	$(CMAKE) --build $(BUILD_DIR) --target clean

distclean:
	@rm -rf $(BUILD_DIR)

run: build
	@./$(BUILD_DIR)/flight_logger_smoke




# sudo apt update
# sudo apt install software-properties-common
# sudo add-apt-repository ppa:ubuntu-toolchain-r/test
# sudo apt update
# sudo apt install gcc-12 g++-12
