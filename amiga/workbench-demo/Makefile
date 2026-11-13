CC = m68k-amigaos-gcc
CFLAGS = -noixemul -m68020 -msoft-float -Os -Wall -Wextra -Werror
OUT ?= ../../.context/amiga-demo

.PHONY: all
all: $(OUT)/TermicDemo

$(OUT)/TermicDemo: main.c Makefile
	mkdir -p $(OUT)
	$(CC) $(CFLAGS) main.c -o $@ -lamiga
