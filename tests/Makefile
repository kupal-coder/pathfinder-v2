CXX      ?= g++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -Wno-unused-parameter
INCLUDES := -I../src -I.
CORE     := ../src/solver/Types.cpp ../src/solver/Physics.cpp ../src/solver/Planner.cpp ../src/bot/AutoPlayer.cpp
HDRS     := $(wildcard ../src/*/*.hpp)

all: pathfinder_tests bench

pathfinder_tests: main.cpp LevelBuilder.hpp $(CORE) $(HDRS)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -o $@ main.cpp $(CORE)

bench: bench.cpp LevelBuilder.hpp $(CORE) $(HDRS)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -o $@ bench.cpp $(CORE)

test: pathfinder_tests
	./pathfinder_tests

run: test bench
	./bench

# Syntax-checks the mod's Geode glue against a stub of the Geode/GD API
# (tools/geode-check). Catches API misuse without needing the SDK installed.
GEODE_SRC := ../src/geode/LevelScanner.cpp ../src/geode/State.cpp \
             ../src/geode/Hud.cpp ../src/geode/Hooks.cpp ../src/main.cpp

geode-check:
	@set -e; for f in $(GEODE_SRC); do \
		echo "checking $$f"; \
		$(CXX) -std=c++23 -fsyntax-only -DPF_STUB_BUILD -I../tools/geode-check -I../src \
			-Wall -Wextra -Wno-unused-parameter $$f; \
	done; echo "geode glue OK"

check: geode-check test bench

clean:
	rm -f pathfinder_tests bench

.PHONY: all test run clean check geode-check
