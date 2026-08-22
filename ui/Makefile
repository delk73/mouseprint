CXX ?= g++
PKG_CONFIG ?= pkg-config
MOC ?= /usr/lib/qt6/moc

TARGET := ../build/ui-query-test
GUI_TARGET := ../build/mouseprint-inspector
FIXTURE := $(CURDIR)/tests/fixture.sql
SOURCES := src/query_repository.cpp tests/query_repository_test.cpp
GUI_SOURCES := src/main.cpp src/inspection_controller.cpp src/query_repository.cpp
MOC_SOURCE := ../build/inspection_controller.moc.cpp
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -Wpedantic
CXXFLAGS += -Isrc $(shell $(PKG_CONFIG) --cflags sqlite3)
LDLIBS := $(shell $(PKG_CONFIG) --libs sqlite3)
QT_CXXFLAGS := $(shell $(PKG_CONFIG) --cflags Qt6Core Qt6Gui Qt6Qml Qt6Quick Qt6QuickControls2)
QT_LDLIBS := $(shell $(PKG_CONFIG) --libs Qt6Core Qt6Gui Qt6Qml Qt6Quick Qt6QuickControls2)

.PHONY: all test clean

all: $(TARGET) $(GUI_TARGET)

test: $(TARGET)
	$(TARGET) "$(FIXTURE)"

$(TARGET): $(SOURCES) src/query_repository.h src/ui_models.h
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $(SOURCES) $(LDLIBS) -o $@

$(GUI_TARGET): $(GUI_SOURCES) $(MOC_SOURCE) src/query_repository.h src/ui_models.h \
		src/inspection_controller.h qml/Main.qml
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $(QT_CXXFLAGS) $(GUI_SOURCES) $(MOC_SOURCE) $(LDLIBS) $(QT_LDLIBS) -o $@

$(MOC_SOURCE): src/inspection_controller.h
	@mkdir -p $(@D)
	$(MOC) $< -o $@

clean:
	rm -f $(TARGET) $(GUI_TARGET) $(MOC_SOURCE)
