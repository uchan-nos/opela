#!/bin/bash

clang++-10 -O3 -std=c++20 -Wall -Wextra -o opelac main.cpp || exit 1

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

build_run 42 42
build_run 11 "1+23 - 13"

echo "$passed passed, $failed failed"
if [ $failed -ne 0 ]
then
  exit 1
fi
