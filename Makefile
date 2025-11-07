# Simple test runner Makefile for VietVM projects
# Usage:
#   make           # builds (if necessary) and runs tests
#   make build     # configure & build with CMake
#   make test      # alias for 'check'
#   make check     # run all .vi tests in $(TESTDIR)
#   make clean     # remove test outputs
#   make distclean # remove build directory and test outputs

VMSRC ?= VietVM
# build directory used by CMake
BUILD_DIR ?= build
# Try to locate typical binary names produced by various CMake setups.
BIN := $(firstword $(wildcard $(BUILD_DIR)/$(VMSRC) $(BUILD_DIR)/vietvm-cli $(BUILD_DIR)/VietVM $(BUILD_DIR)/vietvm-cli cmake-build-debug/$(VMSRC) cmake-build-debug/vietvm-cli))

TESTDIR ?= tests
EXPECTEDDIR ?= $(TESTDIR)/expected
TESTFILES := $(wildcard $(TESTDIR)/*.vi)

.PHONY: all build test check clean distclean run

all: build check

build:
	@mkdir -p $(BUILD_DIR)
	@echo "Configuring with CMake into $(BUILD_DIR)..."
	@cmake -S . -B $(BUILD_DIR)
	@echo "Building..."
	@cmake --build $(BUILD_DIR) -- -j$(shell nproc 2>/dev/null || echo 4)

# alias
test: check

check: build
	@if [ -z "$(TESTFILES)" ]; then \
		echo "No test files found in $(TESTDIR)"; \
		exit 0; \
	fi
	@for testfile in $(TESTFILES); do \
		base=$$(basename $${testfile%.vi}); \
		out=$(TESTDIR)/$$base.output; \
		exp=$(EXPECTEDDIR)/$$base.expected; \
		echo "== Running $$testfile =="; \
		# pick BIN if it's been found by wildcard; otherwise try default path
		if [ -n "$(BIN)" ]; then \
			EXEC=$(BIN); \
		else \
			EXEC=$(BUILD_DIR)/$(VMSRC); \
		fi; \
		if [ ! -x $$EXEC ]; then \
			echo "Executable not found or not executable: $$EXEC"; \
			echo "Run 'make build' or adjust VMSRC/BUILD_DIR variables."; \
			exit 2; \
		fi; \
		$$EXEC $$testfile > $$out 2>&1; \
		if [ -f $$exp ]; then \
			if diff -u $$exp $$out; then \
				echo "PASS: $$testfile"; \
			else \
				echo "FAIL: $$testfile"; \
				exit 1; \
			fi; \
		else \
			echo "No expected file ($$exp), skipping compare"; \
		fi; \
	done

# Run a single test: make run TEST=tests/example.vi
run:
	@if [ -z "$(TEST)" ]; then \
		echo "Usage: make run TEST=tests/your_test.vi"; \
		exit 1; \
	fi
	@if [ -n "$(BIN)" ]; then \
		EXEC=$(BIN); \
	else \
		EXEC=$(BUILD_DIR)/$(VMSRC); \
	fi; \
	if [ ! -x $$EXEC ]; then \
		echo "Executable not found: $$EXEC"; exit 2; fi; \
	$$EXEC $(TEST)

clean:
	@echo "Removing test outputs..."
	@rm -f $(TESTDIR)/*.output

distclean: clean
	@echo "Removing build directory $(BUILD_DIR)..."
	@rm -rf $(BUILD_DIR)

# helpful debug target
show:
	@echo "BUILD_DIR = $(BUILD_DIR)"
	@echo "VMSRC     = $(VMSRC)"
	@echo "BIN       = $(BIN)"
	@echo "TESTDIR   = $(TESTDIR)"
	@echo "TESTFILES = $(TESTFILES)"