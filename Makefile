VMSRC = VietVM
TESTDIR = tests
EXPECTEDDIR = tests/expected
TESTFILES = $(wildcard $(TESTDIR)/*.vi)

.PHONY: test check clean

test: check

check:
	@if [ ! -x ./cmake-build-debug/$(VMSRC) ]; then \
		echo "Error: binary ./cmake-build-debug/$(VMSRC) not found or not executable"; exit 2; \
	fi
	@for testfile in $(TESTFILES); do \
		base=$$(basename $${testfile%.vi}); \
		out=$(TESTDIR)/$$base.output; \
		exp=$(EXPECTEDDIR)/$$base.expected; \
		echo "== Running $$testfile =="; \
		# Capture both stdout and stderr so debug logs / errors go into the .output file
		./cmake-build-debug/$(VMSRC) $$testfile > $$out 2>&1; \
		status=$$?; \
		if [ $$status -ne 0 ]; then \
			echo "ERROR: test $$testfile exited with status $$status (see $$out)"; \
			exit $$status; \
		fi; \
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

clean:
	rm -f $(TESTDIR)/*.output