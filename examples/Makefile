CXX := g++
CXXFLAGS := -std=c++20 -Wall -Wextra
LIBS := -lsmb_native -ltbb

SRC := $(wildcard src/*.cpp)
TARGET := build/main

DEBUG ?= 0
ifeq ($(DEBUG), 1)
	CXXFLAGS += -O3 -g -fsanitize=address,undefined -fno-omit-frame-pointer
else
	CXXFLAGS += -O3 -march=native -flto
endif

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(SRC)
	mkdir -p build
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LIBS)

clean:
	rm -f $(TARGET)