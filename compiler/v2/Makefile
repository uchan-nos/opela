CXXFLAGS = -O3 -std=c++20 -Wall -Wextra -g
CFLAGS = -O3 -std=c11 -Wall -Wextra
OBJS = main.o source.o token.o
DEPENDS = $(join $(dir $(OBJS)),$(addprefix .,$(notdir $(OBJS:.o=.d))))

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

.PHONY: depends
depends:
	$(MAKE) $(DEPENDS)

-include $(DEPENDS)
