#!/bin/sh

# This file builds a debug distribution of C13, that dynamically links Qt, and includes debug assertions.

# Currently it will generate a working executable using a distribution of Qt installed via homebrew (brew install qt)
# but app bundle generation appears to be broken

RUST_TARGET_PATH="$PWD/target/macOS-AppleClang-Native-Debug-Dyn/target/debug"

mkdir target

pushd rust
cargo build -p fe56_capi --target-dir ../target/macOS-AppleClang-Native-Debug-Dyn/target
popd

pushd cpp/c13
cmake -S . -B ../../target/macOS-AppleClang-Native-Debug-Dyn/build -DRUST_TARGET_PATH=$RUST_TARGET_PATH
popd

pushd target/macOS-AppleClang-Native-Debug-Dyn/build
cmake --build . --config Debug --parallel
# cmake --install . --config Debug --prefix ../install
popd
