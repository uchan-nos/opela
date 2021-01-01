.PHONY: all
all: sdl

ifeq ($(shell uname -m),arm64)
ARCH = aarch64
else
ARCH = x86_64
endif

sdl: sdl.o
	cc $(LDFLAGS) -o $@ $< -lSDL2

sdl.s: sdl.opl
	cat sdl.opl | ../../opelac -target-arch $(ARCH) > sdl.s

.PHONY: clean
clean:
	rm sdl.s sdl.o
