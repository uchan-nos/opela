CXXFLAGS = -O3 -std=c++17 -Wall -Wextra -g -stdlib=libc++
CFLAGS = -O3 -std=c11 -Wall -Wextra
OBJS = main.o source.o token.o ast.o
DEPENDS = $(join $(dir $(OBJS)),$(addprefix .,$(notdir $(OBJS:.o=.d))))

opelac: $(OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $^

.PHONY: test test-x86_64 test-aarch64
test: test-x86_64

test-x86_64: opelac cfunc.o
	./test.sh x86_64

test-aarch64: opelac cfunc.o
	./test.sh aarch64

.PHONY: clean
clean:
	rm -rf opelac *.o

.%.d: %.cpp
	$(CXX) $(CXXFLAGS) -MM $< > $@

.PHONY: depends
depends:
	$(MAKE) $(DEPENDS)

-include $(DEPENDS)
