CXX ?= g++
AR ?= ar
CXXFLAGS ?= -O2 -g
CXXFLAGS += -std=c++17 -Wall -Wextra -Wpedantic -Ithird_party -Isrc -MMD -MP
LDFLAGS ?=
BUILD := build
NCURSES_LIBS := $(shell pkg-config --libs ncursesw 2>/dev/null || echo -lncurses)
CURL_LIBS := $(shell pkg-config --libs libcurl 2>/dev/null || echo -lcurl)
CORE_LIBS := $(CURL_LIBS) -pthread

CORE_SRCS := $(wildcard src/core/*.cpp)
CORE_OBJS := $(patsubst src/%.cpp,$(BUILD)/%.o,$(CORE_SRCS))
CORE_LIB := $(BUILD)/libstcore.a
TEST_SRCS := $(wildcard tests/test_*.cpp)
TEST_OBJS := $(patsubst tests/%.cpp,$(BUILD)/tests/%.o,$(TEST_SRCS))

.PHONY: all menuconfig defconfig test smoke clean help

all: $(BUILD)/sillytavern-port

$(BUILD):
	@mkdir -p $(BUILD)

$(BUILD)/menuconfig: tools/menuconfig.cpp | $(BUILD)
	$(CXX) $(CXXFLAGS) $< -o $@ $(NCURSES_LIBS)

$(BUILD)/.configured: $(BUILD)/menuconfig features/features.json
	$(BUILD)/menuconfig --defaults --config .config --header $(BUILD)/features.h
	@touch $@

.config $(BUILD)/features.h: $(BUILD)/.configured

-include .config

$(BUILD)/core/%.o: src/core/%.cpp | $(BUILD)
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(CORE_LIB): $(CORE_OBJS)
	$(AR) rcs $@ $^

$(BUILD)/tests/%.o: tests/%.cpp | $(BUILD)
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -Itests -c $< -o $@

$(BUILD)/sillytavern-port: src/main.cpp $(BUILD)/features.h $(CORE_LIB) | $(BUILD)
	$(CXX) $(CXXFLAGS) src/main.cpp $(CORE_LIB) -o $@ $(CORE_LIBS)

$(BUILD)/run_tests: $(TEST_OBJS) $(CORE_LIB) | $(BUILD)
	$(CXX) $(CXXFLAGS) $(TEST_OBJS) $(CORE_LIB) -o $@ $(CORE_LIBS)

test: $(BUILD)/run_tests
	./$(BUILD)/run_tests

smoke: $(BUILD)/sillytavern-port
	@echo "smoke: not available until the GTK UI stage"

menuconfig: $(BUILD)/menuconfig
	./$(BUILD)/menuconfig

defconfig: $(BUILD)/menuconfig
	./$(BUILD)/menuconfig --defaults --config .config --header $(BUILD)/features.h
	@touch $(BUILD)/.configured

clean:
	rm -rf $(BUILD)

help:
	@echo "Targets:"
	@echo "  make              build the application"
	@echo "  make menuconfig   edit the feature configuration"
	@echo "  make defconfig    reset to all ported critical features"
	@echo "  make test         build and run unit tests"
	@echo "  make smoke        run the GUI smoke test (GTK stage)"
	@echo "  make clean        remove build artifacts"

-include $(wildcard $(BUILD)/core/*.d $(BUILD)/tests/*.d)
