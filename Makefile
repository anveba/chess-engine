CC := g++
FLAGS := -Wall -std=c++17 -march=native
INCLUDE := -Isrc

RELEASE_FLAGS := -O3 -flto -DNDEBUG
DEBUG_FLAGS := -O1 -g
PROFILE_FLAGS := -O2 -g -p -DNDEBUG
STATS_FLAGS := -O3 -DNDEBUG -DSEARCH_STATS
SANITIZE_FLAGS := -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer
TSAN_FLAGS := -O1 -g -fsanitize=thread
TEST_FLAGS := -O2 -g -DNO_MAIN -Itests/unit -DTEST_DATA_DIR=\"$(CURDIR)/tests/data\"

BIN_PATH := bin

SRC := $(wildcard src/*.cpp)
HEADERS := $(wildcard src/*.h)
NETS := $(wildcard nnue/*.nnue)
TEST_SRC := $(wildcard tests/unit/*.cpp)
TEST_HEADERS := $(wildcard tests/unit/*.h)

default: release

.PHONY: release debug profile stats sanitize tsan tune
release: $(BIN_PATH)/chess          # Optimised engine.
debug: $(BIN_PATH)/chess-debug      # Asserts on, debug info, no profiling.
profile: $(BIN_PATH)/chess-profile  # For gprof. Writes gmon.out when run.
stats: $(BIN_PATH)/chess-stats      # Prints search statistics after each iteration.
sanitize: $(BIN_PATH)/chess-asan    # AddressSanitizer and UndefinedBehaviorSanitizer.
tsan: $(BIN_PATH)/chess-tsan        # ThreadSanitizer.
tune: $(BIN_PATH)/chess-tune        # Tunable constants exposed as UCI options, for SPSA.

$(BIN_PATH):
	@mkdir -p $(BIN_PATH)

$(BIN_PATH)/chess: $(SRC) $(HEADERS) $(NETS) | $(BIN_PATH)
	$(CC) -o $@ $(SRC) $(FLAGS) $(INCLUDE) $(RELEASE_FLAGS)

$(BIN_PATH)/chess-debug: $(SRC) $(HEADERS) $(NETS) | $(BIN_PATH)
	$(CC) -o $@ $(SRC) $(FLAGS) $(INCLUDE) $(DEBUG_FLAGS)

$(BIN_PATH)/chess-profile: $(SRC) $(HEADERS) $(NETS) | $(BIN_PATH)
	$(CC) -o $@ $(SRC) $(FLAGS) $(INCLUDE) $(PROFILE_FLAGS)

$(BIN_PATH)/chess-stats: $(SRC) $(HEADERS) $(NETS) | $(BIN_PATH)
	$(CC) -o $@ $(SRC) $(FLAGS) $(INCLUDE) $(STATS_FLAGS)

$(BIN_PATH)/chess-asan: $(SRC) $(HEADERS) $(NETS) | $(BIN_PATH)
	$(CC) -o $@ $(SRC) $(FLAGS) $(INCLUDE) $(SANITIZE_FLAGS)

$(BIN_PATH)/chess-tsan: $(SRC) $(HEADERS) $(NETS) | $(BIN_PATH)
	$(CC) -o $@ $(SRC) $(FLAGS) $(INCLUDE) $(TSAN_FLAGS)

$(BIN_PATH)/chess-tune: $(SRC) $(HEADERS) $(NETS) | $(BIN_PATH)
	$(CC) -o $@ $(SRC) $(FLAGS) $(INCLUDE) $(RELEASE_FLAGS) -DTUNE

# Unit tests are built with asserts on.
$(BIN_PATH)/tests: $(SRC) $(HEADERS) $(NETS) $(TEST_SRC) $(TEST_HEADERS) | $(BIN_PATH)
	$(CC) -o $@ $(SRC) $(TEST_SRC) $(FLAGS) $(INCLUDE) $(TEST_FLAGS)

$(BIN_PATH)/tests-asan: $(SRC) $(HEADERS) $(NETS) $(TEST_SRC) $(TEST_HEADERS) | $(BIN_PATH)
	$(CC) -o $@ $(SRC) $(TEST_SRC) $(FLAGS) $(INCLUDE) $(TEST_FLAGS) $(SANITIZE_FLAGS)

.PHONY: test test-unit test-uci test-sanitize test-tsan test-all
test: test-unit test-uci

test-unit: $(BIN_PATH)/tests
	./$(BIN_PATH)/tests

test-uci: $(BIN_PATH)/chess
	python3 tests/uci_test.py ./$(BIN_PATH)/chess

test-sanitize: $(BIN_PATH)/tests-asan $(BIN_PATH)/chess-asan
	./$(BIN_PATH)/tests-asan
	python3 tests/uci_test.py ./$(BIN_PATH)/chess-asan

test-tsan: $(BIN_PATH)/chess-tsan
	TSAN_OPTIONS=halt_on_error=1 setarch $$(uname -m) -R python3 tests/uci_test.py ./$(BIN_PATH)/chess-tsan

test-all: test test-sanitize test-tsan

BASE ?= HEAD
OPENINGS ?= tests/data/positions.txt
.PHONY: sprt
sprt:
	python3 tools/sprt/sprt.py --base $(BASE) --openings $(OPENINGS) --output-dir tools/sprt/results \
		$(if $(FASTCHESS),--fastchess $(FASTCHESS)) $(SPRT_ARGS)

.PHONY: clean
clean:
	rm -rf $(BIN_PATH) gmon.out
