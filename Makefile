RACK_DIR ?= ../Rack-SDK

FLAGS += -Isrc
CFLAGS +=
CXXFLAGS +=
LDFLAGS +=

SOURCES += $(wildcard src/*.cpp)

DISTRIBUTABLES += res
DISTRIBUTABLES += $(wildcard LICENSE*)

include $(RACK_DIR)/plugin.mk

# Unit tests for src/core, which has no Rack dependency.
.PHONY: test
test: build/test_core
	./build/test_core

build/test_core: tests/test_core.cpp $(wildcard src/core/*.hpp)
	@mkdir -p build
	$(CXX) -std=c++11 -Wall -Wextra -O1 -g -Isrc/core -o $@ $<
