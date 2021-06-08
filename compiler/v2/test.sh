#!/bin/bash

target_arch=$1
if [ "$target_arch" = "" ]
then
  echo "Usage: $0 <target arch>"
  exit 1
fi

passed=0
failed=0
opelac="./opelac -target-arch $target_arch"

function build_tmp() {
  echo "$1" | $opelac > tmp.s
  cc -o tmp tmp.s cfunc.o
}

function test_stdout() {
  want="$1"
  input="$2"

  build_tmp "$input"
  got=$(./tmp)
  rm tmp tmp.s

  if [ "$want" = "$got" ]
  then
    echo "[  OK  ]: $input -> '$got'"
    (( ++passed ))
  else
    echo "[FAILED]: $input -> '$got', want '$want'"
    (( ++failed ))
  fi
}

function test_argv() {
  want="$1"
  input_arg="$2"
  input_src="$3"

  build_tmp "$input_src"
  ./tmp $input_arg
  got=$?
  rm tmp tmp.s

  if [ $want -eq $got ]
  then
    echo "[  OK  ]: '$input_arg' -> $got"
    (( ++passed ))
  else
    echo "[FAILED]: '$input_arg' -> $got, want $want"
    (( ++failed ))
  fi
}

make test.exe || exit 1

echo "Running standard testcases..."
./test.exe

echo "============================="
echo "Running extra testcases..."
test_stdout 'foo' 'func main() { write(1, "foo", 3); } extern "C" write func();'

echo "$passed passed, $failed failed"
if [ $failed -ne 0 ]
then
  exit 1
fi

exit 0
