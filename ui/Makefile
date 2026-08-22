CXX ?= g++
PKG_CONFIG ?= pkg-config

TARGET := ../build/ui-query-test
FIXTURE := $(CURDIR)/tests/fixture.sql
SOURCES := src/query_repository.cpp tests/query_repository_test.cpp
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -Wpedantic
CXXFLAGS += -Isrc $(shell $(PKG_CONFIG) --cflags sqlite3)
LDLIBS := $(shell $(PKG_CONFIG) --libs sqlite3)

.PHONY: all test clean

all: $(TARGET)

test: $(TARGET)
	$(TARGET) "$(FIXTURE)"

$(TARGET): $(SOURCES) src/query_repository.h src/ui_models.h
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $(SOURCES) $(LDLIBS) -o $@

clean:
	rm -f $(TARGET)
