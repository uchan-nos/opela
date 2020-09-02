#!/bin/bash

passed=0
failed=0

function build_run() {
  want="$1"
  input="$2"

  echo "$input" | ./opelac > tmp.s
  nasm -f elf64 -o tmp.o tmp.s
  cc -o tmp tmp.o
  ./tmp
  got=$?

  if [ "$want" = "$got" ]
  then
    echo "[  OK  ]: $input -> $got"
    (( ++passed ))
  else
    echo "[FAILED]: $input -> $got, want $want"
    (( ++failed ))
  fi

  rm tmp tmp.o tmp.s
}

build_run 42 "42;"
build_run 11 "1+23 - 13;"
build_run 2 "12/2 - 2 * (3 - 1);"
build_run 5 "-3 + (+8);"
build_run 0 "3 < 1;"
build_run 1 "2*3 >= 13/2;"
build_run 1 "2>2 == 4<=3;"
build_run 0 ""
build_run 3 "42; 3;"

echo "$passed passed, $failed failed"
if [ $failed -ne 0 ]
then
  exit 1
fi
