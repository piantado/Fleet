# ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
# Fleet unit tests.
#
#   make              compile (in parallel) and then run every Test*.cpp in this directory,
#                     printing one line per test:  <TestName> ..... PASS|FAIL (...)
#   make TestNode     compile and run just one test
#   make build        only compile
#   make clean
#
# Each test writes its details (sections, failing checks, statistics) to logs/<Test>.log and
# compiler output to logs/<Test>.build.log. Exit status is nonzero if any test fails.
#
# Variables you may override:
#   STD=-std=c++23        (Fleet.mk uses -std=c++26; older g++ needs c++23 / c++2c)
#   CXX=clang++           OPT=-O1     JOBS=4
#   FLEET_TEST_SEED=7     (environment) reruns the statistical tests with another seed
#   SANITIZE=1            build with -fsanitize=address,undefined
# ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

FLEET_ROOT = ..
include $(FLEET_ROOT)/Fleet.mk

CXX  ?= g++
STD  ?= -std=c++26
OPT  ?= -O2
JOBS ?= $(shell nproc 2>/dev/null || echo 4)

# Fleet.mk's flags, but with the language standard replaceable via STD
CXXFLAGS_TEST = $(STD) $(filter-out -std=%,$(FLEET_FLAGS)) $(OPT) -g -MMD -MP
ifeq ($(SANITIZE),1)
CXXFLAGS_TEST += -fsanitize=address,undefined -fno-sanitize-recover=undefined
endif

TESTS = $(sort $(basename $(wildcard Test*.cpp)))
BINS  = $(addprefix bin/,$(TESTS))

.PHONY: all test build clean $(TESTS)

all: test

build:
	@mkdir -p bin logs
	@$(MAKE) --no-print-directory -k -j$(JOBS) $(BINS) >/dev/null 2>&1 || true

# Compile one test. Compiler output always goes to logs/<Test>.build.log.
bin/%: %.cpp TestHarness.h Makefile
	@mkdir -p bin logs
	@$(CXX) $< -o $@ $(CXXFLAGS_TEST) $(FLEET_INCLUDE) $(FLEET_LIBS) > logs/$*.build.log 2>&1 \
		|| { rm -f $@; exit 1; }

# Run one compiled test and print exactly one result line. A test that crashes (assert,
# segfault, uncaught exception) prints nothing itself, so we report the exit status for it.
define RUN_ONE
	if [ ! -x bin/$(1) ]; then \
		printf '%s COMPILE FAILED (see logs/%s.build.log)\n' "$$(printf '%-26s' '$(patsubst Test%,%,$(1)) ' | tr ' ' '.')" '$(1)'; \
		false; \
	else \
		out=$$(./bin/$(1) 2> logs/$(1).log); rc=$$?; \
		if [ -n "$$out" ]; then echo "$$out"; \
		else printf '%s FAIL (crashed with exit status %s; see logs/%s.log)\n' "$$(printf '%-26s' '$(patsubst Test%,%,$(1)) ' | tr ' ' '.')" "$$rc" '$(1)'; fi; \
		[ $$rc -eq 0 ]; \
	fi
endef

test: build
	@pass=0; fail=0; \
	for t in $(TESTS); do \
		if $(MAKE) --no-print-directory -s run-$$t; then pass=$$((pass+1)); else fail=$$((fail+1)); fi; \
	done; \
	echo "--------------------------------------------------------------"; \
	echo "$$pass passed, $$fail failed (details in Testing/logs/)"; \
	[ $$fail -eq 0 ]

run-%:
	@$(call RUN_ONE,$*)

# `make TestNode` builds (if needed) and runs a single test
$(TESTS): %:
	@mkdir -p bin logs
	@$(MAKE) --no-print-directory -s bin/$@ || true
	@$(call RUN_ONE,$@)

clean:
	rm -rf bin logs

-include $(wildcard bin/*.d)
