CC      ?= cc
CFLAGS  ?= -std=c11 -Wall -Wextra -O2 -g
LDLIBS  ?= -lm
CXXFLAGS?= -std=c++17 -Wall -Wextra -O2 -g
BUILD   := build

HID_SRC := src/hid/zp_hid.c
DSP_SRC := src/dsp/biquad.c src/dsp/design.c

all: tools

tools: $(BUILD)/zpsniff

$(BUILD)/zpsniff: tools/zpsniff.c $(HID_SRC) src/hid/zp_hid.h | $(BUILD)
	$(CC) $(CFLAGS) -Isrc -o $@ $^ $(LDLIBS)

$(BUILD)/zpmath_test: tests/zpmath_test.c $(DSP_SRC) | $(BUILD)
	$(CC) $(CFLAGS) -Isrc -o $@ $^ $(LDLIBS) $(LDLIBS)

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/zpframe_test: tests/zpframe_test.c $(HID_SRC) | $(BUILD)
	$(CC) $(CFLAGS) -Isrc -o $@ $^ $(LDLIBS)

test: $(BUILD)/zpmath_test $(BUILD)/zpframe_test
	@$(BUILD)/zpframe_test
	@echo
	@$(BUILD)/zpmath_test

clean:
	rm -rf $(BUILD)

.PHONY: all tools test clean
