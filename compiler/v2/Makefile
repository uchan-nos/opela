CXXFLAGS = -O3 -std=c++20 -Wall -Wextra -g
CFLAGS = -O3 -std=c11 -Wall -Wextra
OBJS = main.o source.o token.o ast.o asm.o object.o
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
	rm -rf opelac *.o .*.d

.%.d: %.cpp
	$(CXX) $(CXXFLAGS) -MM $< > $@

%.s: %.cpp
	$(CXX) $(CXXFLAGS) -S -masm=intel -o $@ $<

.PHONY: test
test: opelac
	./test.sh $(ARCH)

.PHONY: asm
asm: $(ASMS)

.PHONY: depends
depends:
	$(MAKE) $(DEPENDS)

-include $(DEPENDS)
