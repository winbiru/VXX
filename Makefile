VMSRC = VietVM
BUILD_DIR ?= build

BIN := $(firstword $(wildcard \
    $(BUILD_DIR)/$(VMSRC) \
    $(BUILD_DIR)/vietvm-cli \
    $(BUILD_DIR)/bin/vietvm-cli \
    $(BUILD_DIR)/bin/$(VMSRC) \
    $(BUILD_DIR)/bin/*vietvm* \
    cmake-build-debug/$(VMSRC) \
    cmake-build-debug/vietvm-cli \
    cmake-build-debug/bin/vietvm-cli))


TESTDIR = ./src/tests
EXPECTEDDIR = ./src/tests/expected
TESTFILES = $(wildcard $(TESTDIR)/*.vi)

.PHONY: test check clean

test: check

check:
	@for testfile in $(TESTFILES); do \
		base=$$(basename $${testfile%.vi}); \
		out=$(TESTDIR)/$$base.output; \
		exp=$(EXPECTEDDIR)/$$base.expected; \
		echo "== Running $$testfile =="; \
		./cmake-build-debug/$(VMSRC) $$testfile > $$out; \
		if [ -f $$exp ]; then \
			if diff -u $$exp $$out; then \
				echo "PASS: $$testfile"; \
        	else \
            	echo "FAIL: $$testfile"; \
                exit 1; \
            fi;\
        else \
            echo "No expected file ($$exp), skipping compare"; \
        fi;\
    done

clean:
	@echo "Removing test outputs..."
	rm -f $(TESTDIR)/*.output

show:
	@echo "BUILD_DIR = $(BUILD_DIR)"
	@echo "VMSRC     = $(VMSRC)"
	@echo "BIN       = $(BIN)"
	@echo "TESTDIR   = $(TESTDIR)"
	@echo "TESTFILES = $(TESTFILES)"
