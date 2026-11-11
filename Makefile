CC      ?= cc
CFLAGS  ?= -std=c11 -O2 -Wall -Wextra
PREFIX  ?= /usr/local

ifeq ($(OS),Windows_NT)
  EXE := .exe
endif
TARGET := antigravity-manager$(EXE)

.PHONY: all test install uninstall clean

all: $(TARGET)

$(TARGET): src/antigravity-manager.c
	$(CC) $(CFLAGS) $< -o $@

test: $(TARGET)
	bash tests/smoke.sh ./$(TARGET)

install: $(TARGET)
	install -Dm755 $(TARGET) $(DESTDIR)$(PREFIX)/bin/$(TARGET)

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/$(TARGET)

clean:
	rm -f antigravity-manager antigravity-manager.exe
