#!/bin/sh
set -e
cd "$(dirname "$0")"
apt-get install -y -q mingw-w64-x86-64-dev g++-mingw-w64-x86-64-posix clang >/dev/null
mkdir -p deps
[ -d deps/imgui ] || (git clone -q https://github.com/ocornut/imgui.git deps/imgui && git -C deps/imgui checkout -q ed73ef4e84c9488256c5710de6ff1ebc2c9a8496)
[ -d deps/minhook ] || (git clone -q https://github.com/TsudaKageyu/minhook.git deps/minhook && git -C deps/minhook checkout -q 8af6b4acae5a9388fd742b56fa79ece89d96f823)
ln -sfn . deps/deps
echo ready
