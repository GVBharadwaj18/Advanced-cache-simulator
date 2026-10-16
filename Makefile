# ─────────────────────────────────────────────────────────────
#  Cache-Simulator  –  Makefile
#  Requires: g++ with C++17 support, GNU make
# ─────────────────────────────────────────────────────────────

CXX      := g++
CXXFLAGS := -std=c++17 -O2 -Wall -Wextra -Wpedantic \
            -Wshadow -Wnon-virtual-dtor -march=native
LDFLAGS  := -lpthread -lstdc++fs

# Directories
SRC_DIR  := src
INC_DIR  := include
OBJ_DIR  := .build
BIN      := cache_sim

# Sources & objects
SRCS := $(wildcard $(SRC_DIR)/*.cpp)
OBJS := $(patsubst $(SRC_DIR)/%.cpp, $(OBJ_DIR)/%.o, $(SRCS))

# Default target
.PHONY: all
all: $(BIN)

# Link
$(BIN): $(OBJS)
	@echo "  LD  $@"
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)
	@echo "  ✔   Build successful → $(BIN)"

# Compile
$(OBJ_DIR)/%.o: $(SRC_DIR)/%.cpp | $(OBJ_DIR)
	@echo "  CXX $<"
	$(CXX) $(CXXFLAGS) -I$(INC_DIR) -c $< -o $@

$(OBJ_DIR):
	@mkdir -p $(OBJ_DIR)

# ─── Convenience targets ─────────────────────────────────────

.PHONY: debug
debug: CXXFLAGS += -g -DDEBUG -O0 -fsanitize=address,undefined
debug: LDFLAGS  += -fsanitize=address,undefined
debug: all

.PHONY: run
run: all
	./$(BIN) configs/default.cfg traces/sample.trace

.PHONY: compare
compare: all
	./$(BIN) configs/default.cfg traces/sample.trace --compare

.PHONY: benchmark
benchmark: all
	./$(BIN) configs/default.cfg traces/large.trace --benchmark

.PHONY: clean
clean:
	@rm -rf $(OBJ_DIR) $(BIN) results/
	@echo "  ✔   Clean complete"

.PHONY: help
help:
	@echo ""
	@echo "  make              – Build the simulator (optimised)"
	@echo "  make debug        – Build with ASAN / UBSAN"
	@echo "  make run          – Build + run with sample trace"
	@echo "  make compare      – Build + run in compare mode"
	@echo "  make benchmark    – Build + run in benchmark mode"
	@echo "  make clean        – Remove build artefacts"
	@echo ""
