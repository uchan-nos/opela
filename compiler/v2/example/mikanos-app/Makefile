# このディレクトリ内のプログラムをビルドするには、事前に
# `source $HOME/osbook/devenv/buildenv.sh`
# を実行し、LDFLAGS に MikanOS 用 Newlib のパスが設定しておく必要があります。

LDFLAGS += --entry main -z norelro --image-base 0xffff800000000000 --static

ifeq ($(shell uname -m),arm64)
ARCH = aarch64
else
ARCH = x86_64
endif

.PHONY: all
all: rpn

.PHONY: clean
clean:
	rm rpn.o rpn.s

rpn: rpn.o syscall.o newlib_support.o
	ld.lld $(LDFLAGS) -o $@ $^ -lc -lc++ -lc++abi -lm

%.o: %.opl
	../../opelac -target-arch $(ARCH) < $< > $*.s
	$(AS) -g -o $@ $*.s
