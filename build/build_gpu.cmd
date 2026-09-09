@echo off
REM ====================================================================================================
REM TAPESTRY GPU build — nvcc, C++17, contraction PINNED.
REM
REM -fmad=false is passed deliberately and is expected to be INERT: the field's arithmetic is entirely
REM integer, so there is no floating-point multiply-add for the compiler to contract. QC-1 and QC-2
REM measured 22 percent divergence between contraction settings on the float version; pinning a flag
REM that cannot matter any more is how you demonstrate that it cannot.
REM
REM   build_gpu.cmd          build bin\t_gpu.exe
REM   build_gpu.cmd test     build, then run R2's gate
REM ====================================================================================================
setlocal
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 (echo vcvars64 failed & exit /b 1)

set ROOT=C:\TAPESTRY
set BIN=%ROOT%\bin
if not exist "%BIN%" mkdir "%BIN%"

REM sm_89 is the estate's RTX 4070 Ti SUPER (Ada). The arch is part of the determinism tuple §5 names,
REM so it is written down here rather than left to nvcc's default.
set NVFLAGS=-std=c++17 -O2 -arch=sm_89 -fmad=false -DNOMINMAX -DWIN32_LEAN_AND_MEAN -D_CRT_SECURE_NO_WARNINGS
set NVFLAGS=%NVFLAGS% -Xcompiler "/utf-8 /EHsc /fp:strict /W3"

echo === compiling t_gpu (nvcc) ===
nvcc %NVFLAGS% "%ROOT%\src\tests\t_gpu.cu" -o "%BIN%\t_gpu.exe"
if errorlevel 1 (echo BUILD FAILED: t_gpu & exit /b 1)

echo === gpu build ok ===
if /I "%1"=="test" (
  "%BIN%\t_gpu.exe"
  if errorlevel 1 (echo GPU GATE FAILED & exit /b 1)
)
exit /b 0
