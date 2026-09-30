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

build/test_core: $(wildcard tests/*.cpp tests/*.hpp src/core/*.hpp)
	@mkdir -p build
	$(CXX) -std=c++11 -Wall -Wextra -O1 -g -Isrc/core -Itests -o $@ $(wildcard tests/*.cpp)
