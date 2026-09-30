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

# Serialization round trip; links libRack for jansson. Running it needs Rack's install
# folder (for libRack.dll) on PATH, but only for running: that folder's DLLs shadow the
# compiler's own, which then fails without a word.
RACK_INSTALL ?= /c/Program Files/VCV/Rack2Pro
.PHONY: test-rack
test-rack: build/test_serialize.exe
	PATH="$(RACK_INSTALL):$$PATH" ./build/test_serialize.exe

build/test_serialize.exe: tests/rack/test_serialize.cpp $(wildcard src/*.hpp src/core/*.hpp)
	@mkdir -p build
	$(CXX) -std=c++11 $(filter-out -municode,$(FLAGS)) -o $@ $< -L$(RACK_DIR) -lRack -static-libstdc++

build/test_core: $(wildcard tests/*.cpp tests/*.hpp src/core/*.hpp)
	@mkdir -p build
	$(CXX) -std=c++11 -Wall -Wextra -O1 -g -Isrc/core -Itests -o $@ $(wildcard tests/*.cpp)
