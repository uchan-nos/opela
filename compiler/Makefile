CXXFLAGS = -O3 -std=c++17 -Wall -Wextra -g
CFLAGS = -O3 -std=c11 -Wall -Wextra
OBJS = main.o source.o token.o ast.o
DEPENDS = $(join $(dir $(OBJS)),$(addprefix .,$(notdir $(OBJS:.o=.d))))

ifeq ($(shell uname -m),arm64)
ARCH = aarch64
else
ARCH = x86_64
endif

opelac: $(OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $^

.PHONY: test test-x86_64 test-aarch64
test:
	make test-$(ARCH)

test-x86_64: opelac cfunc.o
	./test.sh x86_64

test-aarch64: opelac cfunc.o
	./test.sh aarch64

test.run: test_run.opl opelac cfunc.o
	$(CC) -E -x c $< | grep -v '^#' > test_run.tmp
	cat test_run.tmp | ./opelac -target-arch $(ARCH) > test_run.s
	as -o test_run.o test_run.s
	$(CC) -o $@ test_run.o cfunc.o

.PHONY: clean
clean:
	rm -rf opelac *.o

.%.d: %.cpp
	$(CXX) $(CXXFLAGS) -MM $< > $@

.PHONY: depends
depends:
	$(MAKE) $(DEPENDS)

-include $(DEPENDS)
