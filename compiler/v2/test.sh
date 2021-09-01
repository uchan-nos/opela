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

function test_exit() {
  want="$1"
  input="$2"

  build_tmp "$input"
  ./tmp
  got=$?
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

#make test.exe || exit 1

echo "Running standard testcases..."
#./test.exe
test_exit 42 'func main() int { return 42; }'
test_exit 30 'func main() int { return (1+2) / 2+ (( 3 -4) +5 *  6 ); }'
test_exit 5  'func main() int { return -3 + (+8); }'
test_exit 2  'func main() int { return -2 * -1; }'
test_exit 0  'func main() int { return 3 < (1 + 1); }'
test_exit 1  'func main() int { return 3 > (1 + 1); }'
test_exit 1  'func main() int { return 2*3 >= 13/2; }'
test_exit 1  'func main() int { return 2>2 == 4<=3; }'
test_exit 15 'func main() int { foo:=5; bar:=3; return foo*bar; }'
test_exit 6  'func main() int { foo:=2; { foo:=3; } return foo+4; }'
test_exit 42 'func main() int { return 41+1; 3; }'
test_exit 2  'func main() int { if 42 > 10 { return 2; } }'
test_exit 0  'func main() int { if 42 < 10 { return 2; } }'
test_exit 2  'func main() int { cond := 10 < 200; if cond { return 2; } }'
test_exit 4  'func main() int { if 0 { return 3; } else { return 4; } }'
test_exit 5  'func main() int { if 0 { return 3; } else if 1 { return 5; } else { return 4; } }'
test_exit 8  'func main() int { foo := 3; foo = 4; return foo * 2; }'
test_exit 38 'func main() int { foo:=5; bar:=7; foo=(bar=1)=42; return foo-4; }'
test_exit 42 'func main() int { foo:=5; bar:=7; return foo=bar=42; }'
test_exit 9  'func main() int { a:=5; a=b:=3; return a*b; }'
test_exit 55 'func main() int { i:=0; s:=0; for i <= 10 { s=s+i; i=i+1; } return s; }'
test_exit 55 'func main() int { s:=0; for i:=0; i<=10; i=i+1 { s=s+i; } return s; }'
test_exit 9  'func main() int { s:=0; for i:=1;i<3;i=i+1{ for j:=1;j<3;j=j+1{ s=s+i*j; } } return s; }'
test_exit 39 'func main() int { return func42() - 3; } extern "C" func42 func()int;'
test_exit 42 'func main() int { return funcfunc42()(); } extern "C" funcfunc42 func() *func()int;'
test_exit 43 'func main() int { return add((1+2)*(3+4), add(func42(), 1)) - 21; } extern "C" func42 func()int; extern "C" add func(a, b int) int;'
test_exit 4  'func main() int { return sizeof(myInt); } type myInt int32;'
test_exit 6  'func main() int { return (3@myInt2 + 2@int2) + 5; } type myInt2 int2;'

echo "============================="
#echo "Running extra testcases..."
#test_stdout 'foo' 'func main() { write(1, "foo", 3); }
#  extern "C" write func(int, *byte, int);'

echo "$passed passed, $failed failed"
if [ $failed -ne 0 ]
then
  exit 1
fi

exit 0
