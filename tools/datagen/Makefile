CC := g++
FLAGS := -DNO_MAIN -Wall -std=c++17 -march=native
RELEASE_FLAGS := -O3 -flto -DNDEBUG
INCLUDE := -Isrc

ROOT := ../..
HERE := tools/datagen
SRC := src/*.cpp $(HERE)/*.cpp

BIN_PATH := bin
TARGET := $(BIN_PATH)/datagen

default: release

.PHONY: release
release:
	@mkdir -p $(BIN_PATH)
	cd $(ROOT) && $(CC) -o $(HERE)/$(TARGET) $(SRC) $(FLAGS) $(INCLUDE) $(RELEASE_FLAGS)

.PHONY: clean
clean:
	rm -f $(TARGET)
