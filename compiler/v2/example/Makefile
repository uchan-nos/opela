CXXFLAGS = -O3 -std=c++20 -Wall -Wextra -g -stdlib=libc++
CFLAGS = -O3 -std=c11 -Wall -Wextra

ifeq ($(shell uname -m),arm64)
ARCH = aarch64
else
ARCH = x86_64
endif

.PHONY: all
all: rpn sdl list hello cmdargs vector coro

.PHONY: clean
clean:
	rm -rf *.o *.s

rpn: rpn.o

sdl: sdl.o
	$(CC) -o $@ $< $(LDFLAGS) -lSDL2

list: list.o

hello: hello.o

cmdargs: cmdargs.o

vector: vector.o

coro: coro.o mycoroutine_helper.o

../opelac:
	make -C .. opelac

%.o: %.opl ../opelac
	../opelac -target-arch $(ARCH) < $< > $*.s
	$(AS) -g -o $@ $*.s
