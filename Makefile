CC := g++
FLAGS := -Wall -std=c++17
DEBUG_FLAGS = -p -g3 
FAST_DEBUG_FLAGS = -O2
RELEASE_FLAGS = -O3 -flto -DNDEBUG
INCLUDE := -Isrc

CCFLAGS := $(FLAGS) $(INCLUDE)
LDFLAGS := $(FLAGS)

BIN_PATH := bin
OBJ_PATH := obj
SRC_PATH := src

TARGET_NAME := chess
TARGET := $(BIN_PATH)/$(TARGET_NAME)

SRC := $(foreach x, $(SRC_PATH), $(wildcard $(addprefix $(x)/*,.c*)))
OBJ := $(addprefix $(OBJ_PATH)/, $(addsuffix .o, $(notdir $(basename $(SRC)))))

CLEAN_LIST := $(TARGET) $(OBJ)

default: makedir all

.PHONY: makedir
makedir:
	@mkdir -p $(BIN_PATH) $(OBJ_PATH)

.PHONY: all
all: 
	$(CC) -o $(TARGET) $(SRC) $(CCFLAGS) $(FAST_DEBUG_FLAGS)

.PHONY: debug
debug: 
	$(CC) -o $(TARGET) $(SRC) $(CCFLAGS) $(DEBUG_FLAGS)

.PHONY: release
release: 
	$(CC) -o $(TARGET) $(SRC) $(CCFLAGS) $(RELEASE_FLAGS)

.PHONY: clean
clean:
	@echo CLEAN $(CLEAN_LIST)
	@rm -f $(CLEAN_LIST)