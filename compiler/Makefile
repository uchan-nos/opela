CXX = clang++-10
CXXFLAGS = -O3 -std=c++20 -Wall -Wextra -g -stdlib=libc++
CFLAGS = -O3 -std=c11 -Wall -Wextra

opelac: main.cpp source.cpp token.cpp ast.cpp
	$(CXX) $(CXXFLAGS) -o $@ $^

.PHONY: test
test: opelac cfunc.o
	./test.sh

.PHONY: clean
clean:
	rm -rf opelac *.o

%.out: %.opl
	./opelac < $< > $*.s
	nasm -f elf64 -o $*.o $*.s
	cc -no-pie -o $@ $*.o cfunc.o
