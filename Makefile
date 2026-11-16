BUILD_DIR ?= build-pure-vpp
.PHONY: all build test check clean benchmark
all: build
build:
	cmake -S . -B "$(BUILD_DIR)"
	cmake --build "$(BUILD_DIR)" --parallel
check test: build
	ctest --test-dir "$(BUILD_DIR)" --output-on-failure
benchmark: build
	VPP_EXEC="$(CURDIR)/$(BUILD_DIR)/bin/vpp" bash benchmark/languages/run.sh
clean:
	cmake --build "$(BUILD_DIR)" --target clean
