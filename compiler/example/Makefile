CXX = clang++-10
CXXFLAGS = -O3 -std=c++20 -Wall -Wextra -g -stdlib=libc++
CFLAGS = -O3 -std=c11 -Wall -Wextra

.PHONY: all
all: rpn

.PHONY: clean
clean:
	rm -rf *.o *.s

rpn: rpn.o

../opelac:
	make -C .. opelac

%.o: %.opl ../opelac
	../opelac < $< > $*.s
	nasm -f elf64 -o $*.o $*.s
