@echo off
rem Native Windows (x64) build of TOTK for debugging the renderer on a PC GPU (docs/WINDOWS.md).
rem   build-win.cmd [configure]
rem Needs VS 2026 with "C++ Clang tools for Windows" (clang-cl), CMake, Ninja. Layout (TOTK_WIN, default E:\totk-win):
rem   recomp\  clone of ~/totk-build/src/recomp (branch totk)    gen\{main,subsdk0,sdk}  the generated code
rem   third_party\Vulkan-Headers-1.4.357, third_party\opus (libopus 1.5.2 built with clang-cl /arch:AVX2, static)
rem   build\  this build      data\TOTK  the PS4 data stage
rem JOBS (default 8): each generated file takes up to ~1.5 GB of memory to compile.
setlocal
if "%TOTK_WIN%"=="" set TOTK_WIN=E:\totk-win
if "%JOBS%"=="" set JOBS=8
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
if "%1"=="configure" goto configure
if exist "%TOTK_WIN%\build\build.ninja" goto build
:configure
cmake -S "%TOTK_WIN%\recomp" -B "%TOTK_WIN%\build" -G Ninja -DNX_GAME=totk -DCMAKE_BUILD_TYPE=Release ^
    -DCMAKE_C_COMPILER=clang-cl -DCMAKE_CXX_COMPILER=clang-cl ^
    -DNX_GEN_DIR="%TOTK_WIN%/gen" -DNX_VULKAN_SDK="%TOTK_WIN%/third_party/Vulkan-Headers-1.4.357" ^
    -DNX_OPUS_DIR="%TOTK_WIN%/third_party/opus" || exit /b 1
:build
ninja -C "%TOTK_WIN%\build" -j%JOBS% -k 0 totk
