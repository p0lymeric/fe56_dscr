@echo on

rem This file builds a debug distribution of C13, that dynamically links Qt and the VC runtime.
rem This distibution includes debug assertions and links with dlls provided by MSVC and the Qt Community Edition installer.

call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat"

rem Used by cmake to locate Qt6
set "Qt6_ROOT=C:\Qt\6.12.0\msvc2022_64"
set "RUST_TARGET_PATH=%CD%\target\Windows-MSVC-x64-Debug-MD\target\debug"

mkdir target

pushd rust
cargo build -p fe56_capi --target-dir ..\target\Windows-MSVC-x64-Debug-MD\target || goto :FAIL
popd

pushd cpp\c13
cmake -S . -B ..\..\target\Windows-MSVC-x64-Debug-MD\build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_EXPORT_COMPILE_COMMANDS=ON -DRUST_TARGET_PATH="%RUST_TARGET_PATH%" || goto :FAIL
popd

pushd target\Windows-MSVC-x64-Debug-MD\build
cmake --build . --parallel || goto :FAIL
cmake --install . --prefix ..\install || goto :FAIL
popd

exit /b 0

:FAIL
set "ERRORLEVELBAK=%ERRORLEVEL%"
exit /b %ERRORLEVELBAK%
