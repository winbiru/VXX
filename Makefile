# Simple test runner Makefile for VietVM projects
# Usage:
#   make           # builds (if necessary) and runs tests
#   make build     # configure & build with CMake
#   make test      # alias for 'check'
#   make check     # run all .vi tests in $(TESTDIR)
#   make clean     # remove test outputs
#   make distclean # remove build directory and test outputs

VMSRC ?= VietVM
BUILD_DIR ?= build

# Try to locate common binary locations (include build/bin where CMake often places executables)
BIN := $(firstword $(wildcard \
    $(BUILD_DIR)/$(VMSRC) \
    $(BUILD_DIR)/vietvm-cli \
    $(BUILD_DIR)/bin/vietvm-cli \
    $(BUILD_DIR)/bin/$(VMSRC) \
    $(BUILD_DIR)/bin/*vietvm* \
    cmake-build-debug/$(VMSRC) \
    cmake-build-debug/vietvm-cli \
    cmake-build-debug/bin/vietvm-cli))

# Tests live in src/tests per your layout
TESTDIR ?= ./src/tests
EXPECTEDDIR ?= $(TESTDIR)/expected

# deferred expansion so wildcard is evaluated at runtime (useful if tests are generated/available only after checkout/build)
TESTFILES = $(wildcard $(TESTDIR)/*.vi)

.PHONY: all build test check clean distclean show

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
	@# ensure the entire recipe runs in one shell invocation by using backslashes on each line
	@if [ -z "$(TESTFILES)" ]; then \
		echo "No test files found in $(TESTDIR)"; \
	else \
		# decide which executable to run (prefer BIN found by wildcard, otherwise probe common paths)
		if [ -n "$(BIN)" ]; then \
			EXEC="$(BIN)"; \
		else \
			if [ -x "$(BUILD_DIR)/bin/vietvm-cli" ]; then \
				EXEC="$(BUILD_DIR)/bin/vietvm-cli"; \
			elif [ -x "$(BUILD_DIR)/vietvm-cli" ]; then \
				EXEC="$(BUILD_DIR)/vietvm-cli"; \
			elif [ -x "cmake-build-debug/bin/vietvm-cli" ]; then \
				EXEC="cmake-build-debug/bin/vietvm-cli"; \
			else \
				EXEC="$(BUILD_DIR)/$(VMSRC)"; \
			fi; \
		fi; \
		if [ ! -x "$$EXEC" ]; then \
			echo "Executable not found or not executable: $$EXEC"; \
			echo "Build dir listing:"; ls -la $(BUILD_DIR) || true; \
			exit 2; \
		fi; \
		for testfile in $(TESTFILES); do \
			base=$$(basename $${testfile%.vi}); \
			out=$(TESTDIR)/$$base.output; \
			exp=$(EXPECTEDDIR)/$$base.expected; \
			echo "== Running $$testfile =="; \
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
		done; \
	fi

# Run a single test: make run TEST=src/tests/example.vi
run:
	@if [ -z "$(TEST)" ]; then \
		echo "Usage: make run TEST=src/tests/kiem_tra_ham.vi"; \
		exit 1; \
	fi; \
	if [ -n "$(BIN)" ]; then \
		EXEC="$(BIN)"; \
	else \
		if [ -x "$(BUILD_DIR)/bin/vietvm-cli" ]; then \
			EXEC="$(BUILD_DIR)/bin/vietvm-cli"; \
		else \
			EXEC="$(BUILD_DIR)/$(VMSRC)"; \
		fi; \
	fi; \
	if [ ! -x "$$EXEC" ]; then \
		echo "Executable not found: $$EXEC"; exit 2; \
	fi; \
	$$EXEC $(TEST)

clean:
	@echo "Removing test outputs..."
	@rm -f $(TESTDIR)/*.output

distclean: clean
	@echo "Removing build directory $(BUILD_DIR)..."
	@rm -rf $(BUILD_DIR)

show:
	@echo "BUILD_DIR = $(BUILD_DIR)"
	@echo "VMSRC     = $(VMSRC)"
	@echo "BIN       = $(BIN)"
	@echo "TESTDIR   = $(TESTDIR)"
	@echo "TESTFILES = $(TESTFILES)"