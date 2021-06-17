CXXFLAGS = -O3 -std=c++20 -Wall -Wextra -g -stdlib=libc++
CFLAGS = -O3 -std=c11 -Wall -Wextra

ifeq ($(shell uname -m),arm64)
ARCH = aarch64
else
ARCH = x86_64
endif

.PHONY: all
all: rpn sdl list

.PHONY: clean
clean:
	rm -rf *.o *.s

rpn: rpn.o

sdl: sdl.o
	$(CC) -o $@ $< $(LDFLAGS) -lSDL2

list: list.o

../opelac:
	make -C .. opelac

%.o: %.opl ../opelac
	../opelac -target-arch $(ARCH) < $< > $*.s
	$(AS) -g -o $@ $*.s
