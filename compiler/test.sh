#!/bin/bash

clang++-10 -O3 -std=c++20 -o opelac main.cpp

function build_run() {
  echo $2 | ./opelac > tmp.s
  nasm -f elf64 -o tmp.o tmp.s
  cc -o tmp tmp.o
  ./tmp
  ANSWER=$?

  if [ $1 = $ANSWER ]
  then
    echo "[  OK  ]: got $ANSWER, input $2"
  else
    echo "[FAILED]: want $1, got $ANSWER, input $2"
  fi

  rm tmp tmp.o tmp.s
}

build_run 42 42
