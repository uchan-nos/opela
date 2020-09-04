#!/bin/bash

passed=0
failed=0

function build_run() {
  want="$1"
  input="$2"

  echo "$input" | ./opelac > tmp.s
  nasm -f elf64 -o tmp.o tmp.s
  cc -o tmp tmp.o cfunc.o
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

build_run 42 "func main() { 42; }"
build_run 11 "func main() { 1+23 - 13; } }"
build_run 2  "func main() { 12/2 - 2 * (3 - 1); }"
build_run 5  "func main() { -3 + (+8); }"
build_run 0  "func main() { 3 < 1; }"
build_run 1  "func main() { 2*3 >= 13/2; }"
build_run 1  "func main() { 2>2 == 4<=3; }"
build_run 0  "func main() { }"
build_run 3  "func main() { 42; 3; }"
build_run 15 "func main() { foo:=5; bar:=3; foo*bar; }"
build_run 3  "func main() { return 3; 42; }"
build_run 2  "func main() { 1; if 42 > 10 { 2; } }"
build_run 1  "func main() { cond := 10 < 1; 1; if cond { 2; } }"
build_run 8  "func main() { foo := 3; foo = 4; foo * 2; }"
build_run 38 "func main() { foo:=5; bar:=7; foo=(bar=1)=42; foo-4; }"
build_run 42 "func main() { foo:=5; bar:=7; foo=bar=42; }"
build_run 55 "func main() { i:=0; s:=0; for i <= 10 { s=s+i; i=i+1; } s; }"
build_run 9  "func main() { a:=5; a=b:=3; a*b; }"
build_run 55 "func main() { s:=0; for i:=0; i<=10; i=i+1 { s=s+i; } }"
build_run 4  "func main() { if 0 { 3; } else { 4; } }"
build_run 5  "func main() { if 0 { 3; } else if 1 { 5; } else { 4; } }"
build_run 42 "func main() { 42; {} }"
build_run 9  "func main() { s:=0; for i:=1;i<3;i=i+1{ for j:=1;j<3;j=j+1{ s=s+i*j; } } s; }"
build_run 39 "func main() { func42() - 3; }"
build_run 42 "func main() { funcfunc42()(); }"
build_run 4  "func main() { add(add(1, -2), 5); }"
build_run 57 "func main(){add(f(), 2);} func f(){s:=0; for i:=1;i<=10;i=i+1 {s=s+i;} s;}"
build_run 5  "func main(){f(-3,8);} func f(a,b){a+b;}"
build_run 21 "func main(){fib(8);} func fib(n){ if n<=1 {n;} else {fib(n-1)+fib(n-2);} }"
build_run 5  "func main(){ i := 42; p := &i; *p = 3; i + 2; }"
build_run 4  "func main(){ i := 42; p := &i; i = 3; j := *p; j + 1; }"
build_run 14 "func main(){ i := 7; p := &i; q := &p; **q + *&i; }"
build_run 3  "func main(){ i:=42; f(&i); i; } func f(p){ *p = 3; }"
build_run 42 "func main() { -+-42; }"
build_run 4  "func main() { var i int; var p *int; i = 42; p = &i; i = 3; *p + 1; }"

echo "$passed passed, $failed failed"
if [ $failed -ne 0 ]
then
  exit 1
fi
