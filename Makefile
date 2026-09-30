CC      ?= cc
CFLAGS  ?= -std=c11 -Wall -Wextra -O2 -g
LDLIBS  ?= -lm
CXXFLAGS?= -std=c++17 -Wall -Wextra -O2 -g
BUILD   := build
X11_LIBS?= -lX11

HID_SRC := src/hid/zp_hid.c
DSP_SRC := src/dsp/biquad.c src/dsp/design.c src/dsp/response.c
GUI_SRC := src/gui/zp84gui.c
DSP_HDR := src/dsp/biquad.h src/dsp/response.h src/dsp/design.h
GUI_HDR := src/gui/bluetooth.h src/gui/speaker_names.h src/gui/protection.h src/gui/console.h src/gui/controls.h src/gui/car_asset.h src/gui/logo_asset.h src/gui/layout.h src/gui/presets.h src/gui/output_marks.h

all: tools $(BUILD)/zp84gui

tools: $(BUILD)/zpsniff

$(BUILD)/zpsniff: tools/zpsniff.c $(HID_SRC) src/hid/zp_hid.h | $(BUILD)
	$(CC) $(CFLAGS) -Isrc -o $@ $(filter %.c,$^) $(LDLIBS)

WEB_SRC := src/web/zp84web.c

web: $(BUILD)/zp84web

$(BUILD)/zp84web: $(WEB_SRC) $(HID_SRC) src/hid/zp_hid.h | $(BUILD)
	$(CC) $(CFLAGS) -Isrc -o $@ $(WEB_SRC) $(HID_SRC) $(LDLIBS)

gui: $(BUILD)/zp84gui

$(BUILD)/zp84-ble.py: src/ble/zp84-ble.py | $(BUILD)
	cp $< $@

$(BUILD)/zp84gui: $(BUILD)/zp84-ble.py $(GUI_SRC) $(GUI_HDR) $(DSP_HDR) $(DSP_SRC) $(HID_SRC) src/dsp/design.h src/hid/zp_hid.h | $(BUILD)
	$(CC) $(CFLAGS) -Isrc -o $@ $(filter %.c,$^) $(X11_LIBS) $(LDLIBS)

$(BUILD)/zpmath_test: tests/zpmath_test.c $(DSP_SRC) $(DSP_HDR) | $(BUILD)
	$(CC) $(CFLAGS) -Isrc -o $@ $(filter %.c,$^) $(LDLIBS) $(LDLIBS)

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/zpframe_test: tests/zpframe_test.c $(HID_SRC) | $(BUILD)
	$(CC) $(CFLAGS) -Isrc -o $@ $(filter %.c,$^) $(LDLIBS)

$(BUILD)/zpgui_data_test: tests/zpgui_data_test.c $(GUI_SRC) $(GUI_HDR) $(DSP_HDR) $(DSP_SRC) $(HID_SRC) | $(BUILD)
	$(CC) $(CFLAGS) -Isrc -o $@ tests/zpgui_data_test.c $(DSP_SRC) $(HID_SRC) $(X11_LIBS) $(LDLIBS)

$(BUILD)/zpresponse_test: tests/zpresponse_test.c $(DSP_SRC) $(DSP_HDR) | $(BUILD)
	$(CC) $(CFLAGS) -Isrc -o $@ $(filter %.c,$^) $(LDLIBS)

test: $(BUILD)/zpresponse_test $(BUILD)/zpmath_test $(BUILD)/zpframe_test $(BUILD)/zpgui_data_test
	@python3 tests/test_ble.py
	@$(BUILD)/zpresponse_test
	@$(BUILD)/zpgui_data_test
	@$(BUILD)/zpframe_test
	@echo
	@$(BUILD)/zpmath_test

clean:
	rm -rf $(BUILD)

.PHONY: all tools gui test clean
