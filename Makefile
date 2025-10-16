VMSRC = VietVM         # Nếu file thực thi là VietVM.exe thì đổi thành VietVM.exe
TESTDIR = tests
TESTFILES = $(wildcard $(TESTDIR)/*.vi)

.PHONY: test check clean

test: check

check:
	@for testfile in $(TESTFILES); do \
		base=$$(basename $${testfile%.vi}); \
        out=$(TESTDIR)/$$base.output; \
        exp=$(EXPECTEDDIR)/$$base.expected; \
		echo "== Running $$testfile =="; \
		./$(VMSRC) $$testfile > $$out; \
		if [ -f $$exp ]; then \
			if diff -u $$exp $$out; then \
				echo "PASS: $$testfile"; \
			else \
				echo "FAIL: $$testfile"; \
				exit 1; \
			fi \
		else \
			echo "No expected file ($$exp), skipping compare"; \
		fi \
	done

clean:
	rm -f $(TESTDIR)/*.output
