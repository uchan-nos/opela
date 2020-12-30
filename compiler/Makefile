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
	./test.sh $(ARCH)

test-x86_64:
	./test.sh x86_64

test-aarch64:
	./test.sh aarch64

test.run: test_run.opl opelac cfunc.o
	$(CC) -E -x c $< | grep -v '^#' > test_run.tmp
	cat test_run.tmp | ./opelac -target-arch $(ARCH) > test_run.s
	$(CC) -o $@ test_run.s cfunc.o

.PHONY: clean
clean:
	rm -rf opelac *.o

.%.d: %.cpp
	$(CXX) $(CXXFLAGS) -MM $< > $@

.PHONY: depends
depends:
	$(MAKE) $(DEPENDS)

-include $(DEPENDS)
