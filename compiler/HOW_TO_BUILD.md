## 依存パッケージ

### On Ubuntu

- clang-10
- libc++-10-dev
- libc++abi-10-dev

Ubuntu 18.04 と 20.04 で動作を確認しています。

### On M1 Mac

まだ不完全ですが、OpeLa コンパイラが M1 Mac（Arm AArch64）向けのアセンブラを出力できるように開発を進めています。
Mac mini (M1, 2020) のデフォルトコンパイラでビルドとテストができることを確認しています。

## opelac のビルド

    $ make

これで、ビルドしたマシン上で動作する opelac が生成されます。

デフォルトのコンパイラだとコンパイル出来ない可能性があります。
以下のように、C++17 以上に対応したコンパイラを CXX 変数で指定してください。

    $ make CXX=clang++-10

## opelac のテスト

x86_64 向けのアセンブラを用いたテスト

    $ make test-x86_64

AArch64 向けのアセンブラを用いたテスト

    $ make test-aarch64

## opelac の使い方

`-target-arch <arch>` オプションで出力するアセンブラのアーキテクチャを指定できます。
デフォルトは `x86_64` です。

| arch    | 説明 |
|---------|------|
| x86_64  | `nasm` 向けのアセンブリ言語コードを出力します |
| aarch64 | M1 Mac 付属の `as` 向けのアセンブリ言語コードを出力します |
