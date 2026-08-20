CXX ?= g++

TARGET := ../build/mouseprint-collector
SOURCE := src/main.cpp
PKG_CONFIG ?= pkg-config

CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -Wpedantic
CXXFLAGS += $(shell $(PKG_CONFIG) --cflags libinput libudev)
LDLIBS := $(shell $(PKG_CONFIG) --libs libinput libudev)

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(SOURCE)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $< $(LDLIBS) -o $@

clean:
	rm -f $(TARGET)
