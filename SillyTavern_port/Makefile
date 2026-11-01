CXX ?= g++
CXXFLAGS ?= -O2 -g
CXXFLAGS += -std=c++17 -Wall -Wextra -Wpedantic -Ithird_party -Isrc -MMD -MP
LDFLAGS ?=
BUILD := build
NCURSES_LIBS := $(shell pkg-config --libs ncursesw 2>/dev/null || echo -lncurses)

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

$(BUILD)/sillytavern-port: src/main.cpp $(BUILD)/features.h | $(BUILD)
	$(CXX) $(CXXFLAGS) $< -o $@ $(LDFLAGS)

$(BUILD)/run_tests: tests/test_main.cpp $(BUILD)/features.h | $(BUILD)
	$(CXX) $(CXXFLAGS) $< -o $@ $(LDFLAGS)

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
