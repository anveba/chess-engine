CC := g++
FLAGS := -DNO_MAIN -Wall -std=c++17 -march=native
DEBUG_FLAGS = -p -g3
RELEASE_FLAGS = -O3 -flto
INCLUDE := -I../../src -I../ 

CCFLAGS := $(FLAGS) $(INCLUDE)

SRC := ../../src/*.cpp ../*.cpp *.cpp

BIN_PATH := bin

TARGET_NAME := vs
TARGET := $(BIN_PATH)/$(TARGET_NAME)

CLEAN_LIST := $(TARGET)

default: release

.PHONY: makedir
makedir:
	@mkdir -p $(BIN_PATH)

.PHONY: release
release: makedir
	$(CC) -o $(TARGET) $(SRC) $(CCFLAGS) $(RELEASE_FLAGS)

.PHONY: debug
debug: makedir
	$(CC) -o $(TARGET) $(SRC) $(CCFLAGS) $(DEBUG_FLAGS)

.PHONY: clean
clean:
	@echo CLEAN $(CLEAN_LIST)
	@rm -f $(CLEAN_LIST)