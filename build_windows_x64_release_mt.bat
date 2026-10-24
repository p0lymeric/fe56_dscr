@echo off

rem This file builds a release distribution of C13, that statically links Qt and the VC runtime.
rem This produces a single executable that only links to OS dlls, at the expense of requiring a custom Qt build.

rem A custom Qt build is required, configured with these flags:
rem     configure.bat -static -static-runtime -release -submodules qtbase,qtwebsockets

rem Used by cmake to locate Qt6
set "Qt6_ROOT=C:\Qt-custom\6.12.0\msvc2026_64_static_MT"
set "RUST_TARGET_PATH=%CD%\target\Windows-MSVC-x64-Release-MT\target\release"

mkdir target

pushd rust
rem fe56.lib does not appear to link with the VC runtime, but we set +crt-static in case it ever does
set "RUSTFLAGS=-C target-feature=+crt-static"
cargo build -p fe56_capi --release --target-dir ..\target\Windows-MSVC-x64-Release-MT\target
popd

pushd cpp\c13
cmake -S . -B ..\..\target\Windows-MSVC-x64-Release-MT\build -A x64 -DRUST_TARGET_PATH="%RUST_TARGET_PATH%"
popd

pushd target\Windows-MSVC-x64-Release-MT\build
cmake --build . --config Release --parallel
cmake --install . --config Release --prefix ..\install
popd

pause
