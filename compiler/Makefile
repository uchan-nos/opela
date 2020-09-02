CXX = clang++-10
CXXFLAGS = -O3 -std=c++20 -Wall -Wextra -g

opelac: main.cpp source.cpp token.cpp ast.cpp
	$(CXX) $(CXXFLAGS) -o $@ $^

.PHONY: test
test: opelac
	./test.sh

.PHONY: clean
clean:
	rm -rf opelac *.o
