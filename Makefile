# Morse Runner - C11 / GTK 3
#
#   make              build build/morserunner
#   make test         build and run the core tests
#   make run          build and start the program
#   make install      install to $(PREFIX): the program, translations (Linux: desktop entry, icon)
#   make lang-check   check that lang/*.ini match the texts of the sources
#   make clean
#
# Linux, macOS (Homebrew) and Windows (MSYS2 UCRT64/MINGW64 shell).
# Options: make DEBUG=1 (no optimization, sanitizers on Linux/macOS), CC=clang, PREFIX=/usr

VERSION   := 2.0.0
PREFIX    ?= /usr/local
BUILD     ?= build
PKG_CONFIG ?= pkg-config

# ---- platform -----------------------------------------------------------------
ifeq ($(OS),Windows_NT)
  PLATFORM := windows
else
  UNAME_S := $(shell uname -s)
  ifeq ($(UNAME_S),Darwin)
    PLATFORM := macos
  else
    PLATFORM := linux
  endif
endif

EXE :=
PLATFORM_LIBS :=
PLATFORM_OBJS :=
ifeq ($(PLATFORM),windows)
  EXE := .exe
  WINDRES ?= windres
  PLATFORM_LIBS := -mwindows -ladvapi32
  PLATFORM_OBJS := $(BUILD)/res/morserunner.res.o
else ifeq ($(PLATFORM),macos)
  PLATFORM_LIBS := -lm -lpthread -framework CoreFoundation -framework CoreAudio -framework AudioToolbox
else
  PLATFORM_LIBS := -lm -lpthread -ldl
endif

# ---- flags --------------------------------------------------------------------
# the core needs only GLib: `make test` works without GTK
ifneq ($(filter-out clean,$(or $(MAKECMDGOALS),all)),)
  # g_string_replace: GLib 2.68; using anything newer is a compile warning
  GLIB_CFLAGS := $(shell $(PKG_CONFIG) --cflags glib-2.0) \
                 -DGLIB_VERSION_MIN_REQUIRED=GLIB_VERSION_2_68 -DGLIB_VERSION_MAX_ALLOWED=GLIB_VERSION_2_68
  GLIB_LIBS   := $(shell $(PKG_CONFIG) --libs glib-2.0)
  ifeq ($(GLIB_LIBS),)
    $(error GLib not found: install the GLib development package and pkg-config)
  endif
endif
ifneq ($(filter-out test clean,$(or $(MAKECMDGOALS),all)),)
  GTK_CFLAGS := $(shell $(PKG_CONFIG) --cflags gtk+-3.0)
  GTK_LIBS   := $(shell $(PKG_CONFIG) --libs gtk+-3.0)
  GLIB_COMPILE_RESOURCES := $(shell $(PKG_CONFIG) --variable=glib_compile_resources gio-2.0)
  ifeq ($(GTK_LIBS),)
    $(error GTK 3 not found: install the GTK 3 development package and pkg-config)
  endif
endif

ifeq ($(DEBUG),1)
  OPT := -O0 -g
  ifneq ($(PLATFORM),windows)
    OPT += -fsanitize=address,undefined
    SAN_LDFLAGS := -fsanitize=address,undefined
  endif
else
  OPT := -O2
endif

STD      := -std=c11
WARN     := -Wall -Wextra
DEPFLAGS  = -MMD -MP
CORE_CFLAGS := $(STD) $(OPT) $(WARN) -Isrc/core $(GLIB_CFLAGS)
APP_CFLAGS  := $(STD) $(OPT) $(WARN) -Isrc -Isrc/core -Ithird_party/miniaudio $(GLIB_CFLAGS) $(GTK_CFLAGS) \
               -DMR_VERSION=\"$(VERSION)\"
# miniaudio is third-party code: build it with its own conventions, quietly
MA_CFLAGS   := $(STD) $(OPT) -w -D_DEFAULT_SOURCE -D_POSIX_C_SOURCE=200809L \
               -DMA_NO_ENCODING -DMA_NO_DECODING -DMA_NO_GENERATION -DMA_NO_RESOURCE_MANAGER \
               -DMA_NO_NODE_GRAPH -DMA_NO_ENGINE
ifeq ($(PLATFORM),macos)
  MA_CFLAGS := $(filter-out -D_POSIX_C_SOURCE=200809L,$(MA_CFLAGS)) -D_DARWIN_C_SOURCE
endif

# ---- sources ------------------------------------------------------------------
CORE_SRCS := $(wildcard src/core/*.c)
APP_SRCS  := src/main.c src/audio/audio_output.c $(wildcard src/ui/*.c)
MA_SRC    := third_party/miniaudio/miniaudio.c

CORE_OBJS := $(CORE_SRCS:%.c=$(BUILD)/%.o)
APP_OBJS  := $(APP_SRCS:%.c=$(BUILD)/%.o) $(BUILD)/res/resources.o
MA_OBJ    := $(BUILD)/third_party/miniaudio/miniaudio.o
CORE_LIB  := $(BUILD)/libmrcore.a

TARGET    := $(BUILD)/morserunner$(EXE)
# the translations next to the program, as the installed ones are
LANG_FILES := $(wildcard lang/*.ini)
BUILD_LANG := $(LANG_FILES:%=$(BUILD)/%)
TEST_BIN  := $(BUILD)/core_tests$(EXE)

.PHONY: all test run install uninstall clean lang-check

all: $(TARGET) $(BUILD_LANG)

$(BUILD)/lang/%.ini: lang/%.ini
	@mkdir -p $(dir $@)
	cp $< $@

$(TARGET): $(APP_OBJS) $(MA_OBJ) $(CORE_LIB) $(PLATFORM_OBJS)
	$(CC) -o $@ $(APP_OBJS) $(MA_OBJ) $(PLATFORM_OBJS) $(CORE_LIB) $(GTK_LIBS) $(PLATFORM_LIBS) $(SAN_LDFLAGS) $(LDFLAGS)

$(CORE_LIB): $(CORE_OBJS)
	$(AR) rcs $@ $^

$(BUILD)/src/core/%.o: src/core/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CORE_CFLAGS) $(DEPFLAGS) $(CFLAGS) -c $< -o $@

$(BUILD)/src/%.o: src/%.c
	@mkdir -p $(dir $@)
	$(CC) $(APP_CFLAGS) $(DEPFLAGS) $(CFLAGS) -c $< -o $@

# the icon is compiled into the program
$(BUILD)/res/resources.c: res/morserunner.gresource.xml res/morserunner.png
	@mkdir -p $(dir $@)
	$(GLIB_COMPILE_RESOURCES) --sourcedir=res --generate-source --target=$@ $<

$(BUILD)/res/resources.o: $(BUILD)/res/resources.c
	$(CC) $(STD) $(OPT) $(GTK_CFLAGS) $(CFLAGS) -c $< -o $@

$(MA_OBJ): $(MA_SRC) third_party/miniaudio/miniaudio.h
	@mkdir -p $(dir $@)
	$(CC) $(MA_CFLAGS) $(CFLAGS) -c $< -o $@

$(BUILD)/res/morserunner.res.o: res/morserunner.rc res/morserunner.ico
	@mkdir -p $(dir $@)
	$(WINDRES) -I res $< -O coff -o $@

# ---- tests --------------------------------------------------------------------
$(TEST_BIN): $(BUILD)/tests/core_tests.o $(CORE_LIB)
	$(CC) -o $@ $^ $(GLIB_LIBS) -lm $(SAN_LDFLAGS) $(LDFLAGS)

$(BUILD)/tests/%.o: tests/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CORE_CFLAGS) $(DEPFLAGS) $(CFLAGS) -c $< -o $@

test: $(TEST_BIN)
	$(TEST_BIN)

lang-check:
	sh tools/lang-check.sh

run: all
	$(TARGET)

# ---- install ------------------------------------------------------------------
install: all
ifeq ($(PLATFORM),macos)
	$(error make install is not supported on macOS: run build/morserunner)
endif
	install -Dm755 $(TARGET) $(DESTDIR)$(PREFIX)/bin/morserunner$(EXE)
	for f in $(LANG_FILES); do install -Dm644 $$f $(DESTDIR)$(PREFIX)/share/morserunner/$$f; done
ifeq ($(PLATFORM),linux)
	install -Dm644 res/morserunner.desktop $(DESTDIR)$(PREFIX)/share/applications/morserunner.desktop
	install -Dm644 res/morserunner.svg $(DESTDIR)$(PREFIX)/share/icons/hicolor/scalable/apps/morserunner.svg
endif

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/morserunner$(EXE)
	rm -rf $(DESTDIR)$(PREFIX)/share/morserunner
	rm -f $(DESTDIR)$(PREFIX)/share/applications/morserunner.desktop
	rm -f $(DESTDIR)$(PREFIX)/share/icons/hicolor/scalable/apps/morserunner.svg

clean:
	rm -rf $(BUILD)

-include $(CORE_OBJS:.o=.d) $(APP_OBJS:.o=.d) $(BUILD)/tests/core_tests.d
