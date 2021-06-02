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

# echo "Running standard testcases..."
# make test.run || exit 1
# ./test.run
#
#echo "============================="
#echo "Running extra testcases..."

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

  if [ $want -eq $got ]
  then
    echo "[  OK  ]: $input -> $got"
    (( ++passed ))
  else
    echo "[FAILED]: $input -> $got, want $want"
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

test_exit 42 '{ 42; }'
test_exit 30 '{ (1+2) / 2+ (( 3 -4) +5 *  6 ); }'
test_exit 5  '{ -3 + (+8); }'
test_exit 2  '{ -2 * -1; }'
test_exit 0  '{ 3 < (1 + 1); }'
test_exit 1  '{ 3 > (1 + 1); }'
test_exit 1  '{ 2*3 >= 13/2; }'
test_exit 1  '{ 2>2 == 4<=3; }'
test_exit 0  '{ }'
test_exit 3  '{ 42; 3; }'
test_exit 15 '{ foo:=5; bar:=3; foo*bar; }'
test_exit 6  '{ foo:=2; { foo:=3; } foo+4; }'
test_exit 42 '{ return 41+1; 3; }'
test_exit 2  '{ 1; if 42 > 10 { 2; } }'
test_exit 2  '{ cond := 10 < 200; if cond { return 2; } }'
test_exit 4  '{ if 0 { 3; } else { 4; } }'
test_exit 5  '{ if 0 { 3; } else if 1 { 5; } else { 4; } }'
test_exit 8  '{ foo := 3; foo = 4; foo * 2; }'
test_exit 38 '{ foo:=5; bar:=7; foo=(bar=1)=42; foo-4; }'
test_exit 42 '{ foo:=5; bar:=7; foo=bar=42; }'
test_exit 55 '{ i:=0; s:=0; for i <= 10 { s=s+i; i=i+1; } s; }'
test_exit 9  '{ a:=5; a=b:=3; a*b; }'
test_exit 55 '{ s:=0; for i:=0; i<=10; i=i+1 { s=s+i; } return s; }'
test_exit 9  '{ s:=0; for i:=1;i<3;i=i+1{ for j:=1;j<3;j=j+1{ s=s+i*j; } } s; }'
test_exit 39 '{ func42() - 3; }'
test_exit 42 '{ funcfunc42()(); }'
test_exit 4  '{ add(add(1, -2), 5); }'
test_exit 4  '{ add(add(func42(), -2), 5); }'

echo "$passed passed, $failed failed"
if [ $failed -ne 0 ]
then
  exit 1
fi

exit 0

# deprecated standard testcases
#test_exit 42 'func main() { 42; }'
#test_exit 11 'func main() { 1+23 - 13; }'
#test_exit 2  'func main() { 12/2 - 2 * (3 - 1); }'
#test_exit 5  'func main() { -3 + (+8); }'
#test_exit 0  'func main() { 3 < 1; }'
#test_exit 1  'func main() { 2*3 >= 13/2; }'
#test_exit 1  'func main() { 2>2 == 4<=3; }'
#test_exit 0  'func main() { }'
#test_exit 3  'func main() { 42; 3; }'
#test_exit 15 'func main() { foo:=5; bar:=3; foo*bar; }'
#test_exit 3  'func main() { return 3; 42; }'
#test_exit 2  'func main() { 1; if 42 > 10 { 2; } }'
#test_exit 1  'func main() { cond := 10 < 1; 1; if cond { 2; } }'
#test_exit 8  'func main() { foo := 3; foo = 4; foo * 2; }'
#test_exit 38 'func main() { foo:=5; bar:=7; foo=(bar=1)=42; foo-4; }'
#test_exit 42 'func main() { foo:=5; bar:=7; foo=bar=42; }'
#test_exit 55 'func main() { i:=0; s:=0; for i <= 10 { s=s+i; i=i+1; } s; }'
#test_exit 9  'func main() { a:=5; a=b:=3; a*b; }'
#test_exit 55 'func main() { s:=0; for i:=0; i<=10; i=i+1 { s=s+i; } }'
#test_exit 4  'func main() { if 0 { 3; } else { 4; } }'
#test_exit 5  'func main() { if 0 { 3; } else if 1 { 5; } else { 4; } }'
#test_exit 42 'func main() { 42; {} }'
#test_exit 9  'func main() { s:=0; for i:=1;i<3;i=i+1{ for j:=1;j<3;j=j+1{ s=s+i*j; } } s; }'
#test_exit 39 'extern "C" func42 func()int; func main() { func42() - 3; }'
#test_exit 42 'extern "C" funcfunc42 func() *func(); func main() { funcfunc42()(); }'
#test_exit 4  'func main() { add(add(1, -2), 5); } extern "C" add func();'
#test_exit 57 'extern "C" add func(); func main(){add(f(), 2);} func f(){s:=0; for i:=1;i<=10;i=i+1 {s=s+i;} s;}'
#test_exit 5  'func main(){f(-3,8);} func f(a,b int){a+b;}'
#test_exit 21 'func main(){fib(8);} func fib(n int)int{ if n<=1 {n;} else {fib(n-1)+fib(n-2);} }'
#test_exit 5  'func main(){ i := 42; p := &i; *p = 3; i + 2; }'
#test_exit 4  'func main(){ i := 42; p := &i; i = 3; j := *p; j + 1; }'
#test_exit 14 'func main(){ i := 7; p := &i; q := &p; **q + *&i; }'
#test_exit 3  'func main(){ i:=42; f(&i); i; } func f(p *int){ *p = 3; }'
#test_exit 42 'func main() { -+-42; }'
#test_exit 4  'func main() { var i int; var p *int = &i; i = 42; i = 3; *p + 1; }'
#test_exit 42 'extern "C" funcfunc42 func() *func() int; func main() { var f *func() func() int; f = &funcfunc42; f()(); }'
#test_exit 16 'func main() { var p *int = 8; p+1; }'
#test_exit 24 'func main() { var p *int = 32; p-1; }'
#test_exit 2  'func main() { var p *int = 8; var q *int = 24; q-p; }'
#test_exit 4  'func main() { p:=alloc4(3,4,5,6); *(p+1); } extern "C" alloc4 func() *int;'
#test_exit 16 'func main() { var i int; 3*sizeof(int) - sizeof(42); }'
#test_exit 4  'func main() { var arr [3]int; for i:=0; i<3; i=i+1 { arr[i]=i*2; } arr[2]; }'
#test_exit 7  'func main() { var arr [3]*func(a,b int); arr[1]=&add; arr[1](3,4); } extern "C" add func();'
#test_exit 42 'func main() { var (v int = val*3;) v; } var val int = 14;'
#test_exit 10 'func main() { a=3; a+c; } var (a int; b = 6; c = b+1;)'
#test_exit 1  'func main() { sizeof(byte); }'
#test_exit 43 'func main() { var c byte = 42; c+1; }'
#test_exit 2  'func main() { var c int64 = 255; c=c+1; c/100; }'
#test_exit 0  'func main() { var c int8 = 255; c=c+1; c/100; }'
#test_exit 2  'func main() { var c byte = 255; i:=c+1; i/100; }'
#test_exit 4  'func main() { var arr [3]int8; arr[0]=-1; arr[1]=5; arr[2]=arr[0]+arr[1]; }'
#test_exit 10 'func main() { var p *int16 = 8; p+1; }'
#test_exit 28 'func main() { var p *int32 = 32; p-1; }'
#test_exit 33 'func main() { "abc!def"[3]; }'
#test_exit 5  'func main() { strlen("world"); } extern "C" strlen func(s *int8)int64;'
#test_exit 6  'func main() { sizeof("abcdef"); }'
#test_exit 10 'func main() { "\n"[0]; }'
#test_exit 4  'func main() { var x [2]int8; x[0]=1; x[1]=2; p:=&x[0]; p[1]=3; x[1]+1; }'
#test_exit 33 'func main() { p := &" !"[0]; p[1]; }'
#test_exit 1  'func main() { p := &" !"[0]; if p[0]==32 { 1; } else { 2; } }'
#test_exit 1  'func main() { var a [1]int8; var b int8 = a[0]; a[0]=1; b=2; a[0]; }'
#test_exit 12 'func main() { i := 1; i += 2; j := 3; i *= 1 + j; }'
#test_exit 33 "func main() { '!' + '\t' - 9; }"
#test_exit 4  'func main() { i := 3; if i==1 && 2==1 || i==3 { 4; } else { 5; } }'
#test_exit 1  'func main() { v := 5 && 6 || 7; }'
#test_exit 2  'func main() { v := 2; 42 || (v = 3); v; }'
#test_exit 2  '/* main func */ func main() { 2; } // comment'
#test_exit 3  'func main() { i := 1; for i < 10 { if i == 3 { break; } i += 1; } i; }'
#test_exit 10 'func main() { i := 1; for i < 10 { i = 10; continue; i = 42; } i; }'
#test_exit 12 'func main() { i := 1; for i = 1; i < 12; i += 1 { continue; } i; }'
#test_exit 10 'func main() { i := 1; for { if i >= 10 { break; } i = 10; continue; i = 42; } i; }'
#test_exit 42 'func main() { i := 1; for { if i >= 42 { break; } i += 1; continue; } i; }'
#test_exit 4  'type myInt int32; func main() { sizeof(myInt); }'
#test_exit 4  'type myInt int32; func main() { var v myInt; v = myInt(3); v + myInt(2) - myInt(1); }'
