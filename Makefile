CC      ?= gcc
CFLAGS  ?= -Os -s -Wall
LDLIBS  = -lhid -lsetupapi -lgdi32 -lshell32 -ladvapi32

m280bat.exe: src/m280bat.c
	$(CC) -municode -mwindows $(CFLAGS) $< -o $@ $(LDLIBS)

clean:
	rm -f m280bat.exe

.PHONY: clean
