.PHONY: all
all: sdl

sdl: sdl.o
	cc -o $@ $< -lSDL2

sdl.s: sdl.opl
	cat sdl.opl | ../../opelac -target-arch $(shell uname -m) > sdl.s

.PHONY: clean
clean:
	rm sdl.s sdl.o
