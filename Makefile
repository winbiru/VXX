# Makefile (sửa lại để tránh lỗi "expecting fi" bằng .ONESHELL)
SHELL := /bin/sh
# Simple test runner Makefile for V++ projects

VMSRC ?= V++
BUILD_DIR ?= build

BIN := $(firstword $(wildcard \
	$(BUILD_DIR)/bin/vpp-cli \
    $(BUILD_DIR)/bin/vietvm-cli \
    $(BUILD_DIR)/bin/$(VMSRC) \
	$(BUILD_DIR)/vpp-cli \
    $(BUILD_DIR)/vietvm-cli \
    $(BUILD_DIR)/$(VMSRC) \
	cmake-build-debug/bin/vpp-cli \
    cmake-build-debug/bin/vietvm-cli \
	cmake-build-debug/vpp-cli \
    cmake-build-debug/$(VMSRC) \
    cmake-build-debug/vietvm-cli))

TESTDIR ?= ./src/tests
EXPECTEDDIR ?= $(TESTDIR)/expected
TESTFILES = $(wildcard $(TESTDIR)/*.vi)
SKIP_CHECK_TESTS ?= $(TESTDIR)/kiem_tra_ham.vi
CHECK_TESTFILES = $(filter-out $(SKIP_CHECK_TESTS),$(TESTFILES))

.PHONY: all build test check clean distclean show

all: build check

build:
	mkdir -p $(BUILD_DIR)
	echo "Configuring with CMake into $(BUILD_DIR)..."
	cmake -S . -B $(BUILD_DIR)
	echo "Building..."
	cmake --build $(BUILD_DIR) -- -j$(shell nproc 2>/dev/null || echo 4)

test: check

check: build
	@set -e; \
	echo "TESTDIR = $(TESTDIR)"; \
	echo "TESTFILES = $(TESTFILES)"; \
	echo "SKIP_CHECK_TESTS = $(SKIP_CHECK_TESTS)"; \
	echo "CHECK_TESTFILES = $(CHECK_TESTFILES)"; \
	EXEC="$(BIN)"; \
	if [ -z "$$EXEC" ]; then \
		if [ -x "$(BUILD_DIR)/bin/vpp-cli" ]; then \
			EXEC="$(BUILD_DIR)/bin/vpp-cli"; \
		elif [ -x "$(BUILD_DIR)/bin/vietvm-cli" ]; then \
			EXEC="$(BUILD_DIR)/bin/vietvm-cli"; \
		elif [ -x "$(BUILD_DIR)/vpp-cli" ]; then \
			EXEC="$(BUILD_DIR)/vpp-cli"; \
		elif [ -x "$(BUILD_DIR)/vietvm-cli" ]; then \
			EXEC="$(BUILD_DIR)/vietvm-cli"; \
		elif [ -x "cmake-build-debug/bin/vpp-cli" ]; then \
			EXEC="cmake-build-debug/bin/vpp-cli"; \
		elif [ -x "cmake-build-debug/bin/vietvm-cli" ]; then \
			EXEC="cmake-build-debug/bin/vietvm-cli"; \
		else \
			EXEC="$(BUILD_DIR)/$(VMSRC)"; \
		fi; \
	fi; \
	if [ -z "$(CHECK_TESTFILES)" ]; then \
		echo "No test files found in $(TESTDIR)"; \
		exit 0; \
	fi; \
	if [ ! -x "$$EXEC" ]; then \
		echo "Executable not found or not executable: $$EXEC"; \
		ls -la $(BUILD_DIR) || true; \
		exit 2; \
	fi; \
	tmpdir=$(TESTDIR)/.tmp; \
	mkdir -p $$tmpdir; \
	for testfile in $(CHECK_TESTFILES); do \
		base=$$(basename $${testfile%.vi}); \
		out=$$tmpdir/$$base.output; \
		exp=$(EXPECTEDDIR)/$$base.expected; \
		echo "== Running $$testfile =="; \
		"$$EXEC" $$testfile > $$out 2>&1; \
		if [ -f $$exp ]; then \
			if diff -u $$exp $$out; then \
				echo "PASS: $$testfile"; \
			else \
				echo "FAIL: $$testfile"; \
				rm -rf $$tmpdir; \
				exit 1; \
			fi; \
		else \
			echo "FAIL: $$testfile (no expected file: $$exp)"; \
			echo "  Run 'make bless' to create expected files"; \
			rm -rf $$tmpdir; \
			exit 1; \
		fi; \
	done; \
	rm -rf $$tmpdir

# Create/update expected output files from current test runs
# Use this when you've verified the output is correct
bless: build
	@set -e; \
	EXEC="$(BIN)"; \
	if [ -z "$$EXEC" ]; then \
		if [ -x "$(BUILD_DIR)/bin/vpp-cli" ]; then \
			EXEC="$(BUILD_DIR)/bin/vpp-cli"; \
		else \
			EXEC="$(BUILD_DIR)/bin/vietvm-cli"; \
		fi; \
	fi; \
	mkdir -p $(EXPECTEDDIR); \
	for testfile in $(TESTFILES); do \
		base=$$(basename $${testfile%.vi}); \
		exp=$(EXPECTEDDIR)/$$base.expected; \
		echo "== Blessing $$testfile =="; \
		"$$EXEC" $$testfile > $$exp 2>&1; \
		echo "Created: $$exp"; \
	done

clean:
	echo "Removing test outputs..."
	rm -rf $(TESTDIR)/.tmp

distclean: clean
	echo "Removing build directory $(BUILD_DIR)..."
	rm -rf $(BUILD_DIR)

show:
	echo "BUILD_DIR = $(BUILD_DIR)"
	echo "VMSRC     = $(VMSRC)"
	echo "BIN       = $(BIN)"
	echo "TESTDIR   = $(TESTDIR)"
	echo "TESTFILES = $(TESTFILES)"
	echo "SKIP_CHECK_TESTS = $(SKIP_CHECK_TESTS)"
	echo "CHECK_TESTFILES = $(CHECK_TESTFILES)"
