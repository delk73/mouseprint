CXX ?= g++

TARGET := ../build/mouseprint-collector
TEST_TARGET := ../build/correlation-test
SOURCE := src/main.cpp
TEST_SOURCE := tests/correlation_test.cpp
PKG_CONFIG ?= pkg-config

CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -Wpedantic
CXXFLAGS += $(shell $(PKG_CONFIG) --cflags libinput libudev sqlite3)
LDLIBS := $(shell $(PKG_CONFIG) --libs libinput libudev sqlite3)

.PHONY: all test clean

all: $(TARGET)

test: $(TEST_TARGET)
	$(TEST_TARGET)

$(TARGET): $(SOURCE)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $< $(LDLIBS) -o $@

$(TEST_TARGET): $(TEST_SOURCE) $(SOURCE)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $< $(LDLIBS) -o $@

clean:
	rm -f $(TARGET) $(TEST_TARGET)
