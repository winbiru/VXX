# Makefile (sửa lại để tránh lỗi "expecting fi" bằng .ONESHELL)
SHELL := /bin/sh
.ONESHELL:
# Simple test runner Makefile for VietVM projects

VMSRC ?= VietVM
BUILD_DIR ?= build

BIN := $(firstword $(wildcard \
    $(BUILD_DIR)/bin/vietvm-cli \
    $(BUILD_DIR)/bin/$(VMSRC) \
    $(BUILD_DIR)/vietvm-cli \
    $(BUILD_DIR)/$(VMSRC) \
    cmake-build-debug/bin/vietvm-cli \
    cmake-build-debug/$(VMSRC) \
    cmake-build-debug/vietvm-cli))

TESTDIR ?= ./src/tests
EXPECTEDDIR ?= $(TESTDIR)/expected
TESTFILES = $(wildcard $(TESTDIR)/*.vi)

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
	# in debug info trước khi chạy
	echo "TESTDIR = $(TESTDIR)"
	echo "TESTFILES = $(TESTFILES)"
	# chọn executable
	if [ -n "$(BIN)" ]; then
		EXEC="$(BIN)"
	else
		if [ -x "$(BUILD_DIR)/bin/vietvm-cli" ]; then
			EXEC="$(BUILD_DIR)/bin/vietvm-cli"
		elif [ -x "$(BUILD_DIR)/vietvm-cli" ]; then
			EXEC="$(BUILD_DIR)/vietvm-cli"
		elif [ -x "cmake-build-debug/bin/vietvm-cli" ]; then
			EXEC="cmake-build-debug/bin/vietvm-cli"
		else
			EXEC="$(BUILD_DIR)/$(VMSRC)"
		fi
	fi

	if [ -z "$(TESTFILES)" ]; then
		echo "No test files found in $(TESTDIR)"
		exit 0
	fi

	if [ ! -x "$$EXEC" ]; then
		echo "Executable not found or not executable: $$EXEC"
		ls -la $(BUILD_DIR) || true
		exit 2
	fi

	for testfile in $(TESTFILES); do
		base=$$(basename $${testfile%.vi})
		out=$(TESTDIR)/$$base.output
		exp=$(EXPECTEDDIR)/$$base.expected
		echo "== Running $$testfile =="
		$$EXEC $$testfile > $$out 2>&1
		if [ -f $$exp ]; then
			if diff -u $$exp $$out; then
				echo "PASS: $$testfile"
			else
				echo "FAIL: $$testfile"
				exit 1
			fi
		else
			echo "No expected file ($$exp), skipping compare"
		fi
	done

clean:
	echo "Removing test outputs..."
	rm -f $(TESTDIR)/*.output

distclean: clean
	echo "Removing build directory $(BUILD_DIR)..."
	rm -rf $(BUILD_DIR)

show:
	echo "BUILD_DIR = $(BUILD_DIR)"
	echo "VMSRC     = $(VMSRC)"
	echo "BIN       = $(BIN)"
	echo "TESTDIR   = $(TESTDIR)"
	echo "TESTFILES = $(TESTFILES)"