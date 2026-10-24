@echo on

rem This file builds a debug distribution of C13, that dynamically links Qt and the VC runtime.
rem This distibution includes debug assertions and links with dlls provided by MSVC and the Qt Community Edition installer.

rem Used by cmake to locate Qt6
set "Qt6_ROOT=C:\Qt\6.12.0\msvc2022_64"
set "RUST_TARGET_PATH=%CD%\target\Windows-MSVC-x64-Debug-MD\target\debug"

mkdir target

pushd rust
cargo build -p fe56_capi --target-dir ..\target\Windows-MSVC-x64-Debug-MD\target
popd

pushd cpp\c13
cmake -S . -B ..\..\target\Windows-MSVC-x64-Debug-MD\build -A x64 -DRUST_TARGET_PATH="%RUST_TARGET_PATH%"
popd

pushd target\Windows-MSVC-x64-Debug-MD\build
cmake --build . --config Debug --parallel
cmake --install . --config Debug --prefix ..\install
popd

pause
