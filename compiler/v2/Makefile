CXXFLAGS = -O0 -std=c++20 -Wall -Wextra -g
CFLAGS = -O3 -std=c11 -Wall -Wextra
OBJS = main.o source.o token.o ast.o asm.o object.o typespec.o
DEPENDS = $(join $(dir $(OBJS)),$(addprefix .,$(notdir $(OBJS:.o=.d))))
ASMS = $(OBJS:.o=.s)

# アーキテクチャ識別
ifeq ($(shell uname -m),arm64)
ARCH = aarch64
else
ARCH = x86_64
endif

# ビルドルール
opelac: $(OBJS) $(DEPENDS)
	$(CXX) $(CXXFLAGS) -Wl,--export-dynamic -o $@ $(OBJS)

.PHONY: clean
clean:
	rm -f opelac *.o .*.d test.opl.tmp test.s

.%.d: %.cpp
	$(CXX) $(CXXFLAGS) -MM $< > $@

%.s: %.cpp
	$(CXX) $(CXXFLAGS) -S -masm=intel -o $@ $<

.PHONY: test
test: opelac cfunc.o
	./test.sh $(ARCH)

test.exe: test.opl opelac cfunc.o
	$(CC) -E -x c $< | grep -v '^#' > test.opl.tmp
	cat test.opl.tmp | ./opelac -target-arch $(ARCH) > test.s
	$(CC) -o $@ test.s cfunc.o

.PHONY: asm
asm: $(ASMS)

.PHONY: depends
depends:
	$(MAKE) $(DEPENDS)

-include $(DEPENDS)
