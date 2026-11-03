CXX ?= g++
AR ?= ar
CXXFLAGS ?= -O2 -g
CXXFLAGS += -std=c++17 -Wall -Wextra -Wpedantic -Ithird_party -Isrc -MMD -MP
LDFLAGS ?=
SANITIZE ?= 0
ifeq ($(SANITIZE),1)
CXXFLAGS += -fsanitize=address,undefined -fno-omit-frame-pointer
LDFLAGS += -fsanitize=address,undefined
endif
BUILD := build
NCURSES_LIBS := $(shell pkg-config --libs ncursesw 2>/dev/null || echo -lncurses)
CURL_LIBS := $(shell pkg-config --libs libcurl 2>/dev/null || echo -lcurl)
CORE_LIBS := $(CURL_LIBS) -pthread
GTKMM_CFLAGS := $(shell pkg-config --cflags gtkmm-3.0 2>/dev/null)
GTKMM_LIBS := $(shell pkg-config --libs gtkmm-3.0 2>/dev/null)

CORE_SRCS := $(wildcard src/core/*.cpp)
CORE_OBJS := $(patsubst src/%.cpp,$(BUILD)/%.o,$(CORE_SRCS))
CORE_LIB := $(BUILD)/libstcore.a
UI_SRCS := $(wildcard src/ui/*.cpp)
UI_OBJS := $(patsubst src/%.cpp,$(BUILD)/%.o,$(UI_SRCS))
TEST_SRCS := $(wildcard tests/test_*.cpp)
TEST_OBJS := $(patsubst tests/%.cpp,$(BUILD)/tests/%.o,$(TEST_SRCS))

.PHONY: all menuconfig defconfig test test-sanitize smoke clean help

all: $(BUILD)/sillytavern-port

$(BUILD):
	@mkdir -p $(BUILD)

$(BUILD)/menuconfig: tools/menuconfig.cpp | $(BUILD)
	$(CXX) $(CXXFLAGS) $< -o $@ $(LDFLAGS) $(NCURSES_LIBS)

$(BUILD)/.configured: $(BUILD)/menuconfig features/features.json
	$(BUILD)/menuconfig --defaults --config .config --header $(BUILD)/features.h
	@touch $@

.config $(BUILD)/features.h: $(BUILD)/.configured

-include .config

$(BUILD)/core/%.o: src/core/%.cpp | $(BUILD)
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

.PHONY: check-gtkmm
check-gtkmm:
	@pkg-config --exists gtkmm-3.0 || { echo "gtkmm-3.0 not found; install gtkmm3 (Arch) or gtkmm3-dev (Alpine)" >&2; exit 1; }

$(BUILD)/ui/%.o: src/ui/%.cpp | $(BUILD) check-gtkmm
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(GTKMM_CFLAGS) -c $< -o $@

$(CORE_LIB): $(CORE_OBJS)
	$(AR) rcs $@ $^

$(BUILD)/tests/%.o: tests/%.cpp | $(BUILD)
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -Itests -c $< -o $@

$(BUILD)/sillytavern-port: src/main.cpp $(UI_OBJS) $(BUILD)/features.h $(CORE_LIB) | check-gtkmm
	$(CXX) $(CXXFLAGS) $(GTKMM_CFLAGS) src/main.cpp $(UI_OBJS) $(CORE_LIB) -o $@ $(LDFLAGS) $(CORE_LIBS) $(GTKMM_LIBS)

$(BUILD)/run_tests: $(TEST_OBJS) $(CORE_LIB) | $(BUILD)
	$(CXX) $(CXXFLAGS) $(TEST_OBJS) $(CORE_LIB) -o $@ $(LDFLAGS) $(CORE_LIBS)

test: $(BUILD)/run_tests
	./$(BUILD)/run_tests

test-sanitize:
	$(MAKE) clean
	$(MAKE) $(BUILD)/run_tests SANITIZE=1
	ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 ./$(BUILD)/run_tests

smoke: $(BUILD)/sillytavern-port
	./tests/smoke_gtk.sh

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
	@echo "  make defconfig    reset to all ported features"
	@echo "  make test         build and run unit tests"
	@echo "  make test-sanitize  build and run unit tests with ASan/UBSan"
	@echo "  make smoke        run the GUI smoke test (GTK stage)"
	@echo "  make clean        remove build artifacts"

-include $(wildcard $(BUILD)/core/*.d $(BUILD)/tests/*.d $(BUILD)/ui/*.d)
