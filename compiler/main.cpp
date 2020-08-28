#include <iostream>

using namespace std;

int main() {
  int x;
  cin >> x;

  cout << "bits 64\nsection .text\n";
  cout << "global main\n";
  cout << "main:\n";
  cout << "    mov rax, " << x << "\n";
  cout << "    ret\n";
}
