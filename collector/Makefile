CXX ?= g++

TARGET := ../build/mouseprint-collector
TEST_TARGET := ../build/correlation-test
MOVEMENT_TEST_TARGET := ../build/movement-test
SOURCE := src/main.cpp src/movement_episodes.cpp
TEST_SOURCE := tests/correlation_test.cpp
MOVEMENT_TEST_SOURCE := tests/movement_test.cpp
PKG_CONFIG ?= pkg-config

CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -Wpedantic
CXXFLAGS += $(shell $(PKG_CONFIG) --cflags libinput libudev sqlite3)
LDLIBS := $(shell $(PKG_CONFIG) --libs libinput libudev sqlite3)

.PHONY: all test clean

all: $(TARGET)

test: $(TEST_TARGET) $(MOVEMENT_TEST_TARGET)
	$(TEST_TARGET)
	$(MOVEMENT_TEST_TARGET)

$(TARGET): $(SOURCE)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $^ $(LDLIBS) -o $@

$(TEST_TARGET): $(TEST_SOURCE) src/movement_episodes.cpp
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $^ $(LDLIBS) -o $@

$(MOVEMENT_TEST_TARGET): $(MOVEMENT_TEST_SOURCE) src/movement_episodes.cpp
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $^ $(LDLIBS) -o $@


clean:
	rm -f $(TARGET) $(TEST_TARGET) $(MOVEMENT_TEST_TARGET)
