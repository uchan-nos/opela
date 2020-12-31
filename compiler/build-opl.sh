#!/bin/sh -e

source=$1
target=$2
if [ $# -le 1 ]
then
  echo "Usaeg: $0 <source> <target>"
  exit 1
fi

make opelac

temp=$(mktemp opelacode_XXXXXX)
asm=$temp.s

mv $temp $asm

cat $source | ./opelac -target-arch $(uname -m) > $asm
cc -o $target $asm cfunc.o
