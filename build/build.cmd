@echo off
REM ====================================================================================================
REM TAPESTRY build — MSVC 2022, C++17, /W4 clean. One translation unit per binary; the store is headers.
REM   build.cmd            build everything into C:\TAPESTRY\bin
REM   build.cmd test       build, then run the oracles
REM ====================================================================================================
setlocal
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 (echo vcvars64 failed & exit /b 1)

set ROOT=C:\TAPESTRY
set BIN=%ROOT%\bin
set OBJ=%ROOT%\build\obj
if not exist "%BIN%" mkdir "%BIN%"
if not exist "%OBJ%" mkdir "%OBJ%"

REM /fp:strict and /Qfast_transcendentals- off: a fold's arithmetic must not be re-associated by the
REM compiler. QC-1 and QC-2 measured 22 percent FMA divergence between contraction settings, so the
REM flag string is part of a fold's identity and is printed into every receipt.
set CFLAGS=/nologo /utf-8 /std:c++17 /O2 /EHsc /W4 /WX /fp:strict /D NOMINMAX /D WIN32_LEAN_AND_MEAN /D _CRT_SECURE_NO_WARNINGS /Fo%OBJ%\

echo === compiling t_tape ===
cl %CFLAGS% "%ROOT%\src\tests\t_tape.cpp" /Fe:"%BIN%\t_tape.exe"
if errorlevel 1 (echo BUILD FAILED: t_tape & exit /b 1)

echo === compiling t_tx ===
cl %CFLAGS% "%ROOT%\src\tests\t_tx.cpp" /Fe:"%BIN%\t_tx.exe"
if errorlevel 1 (echo BUILD FAILED: t_tx & exit /b 1)

echo === compiling t_field ===
cl %CFLAGS% "%ROOT%\src\tests\t_field.cpp" /Fe:"%BIN%\t_field.exe"
if errorlevel 1 (echo BUILD FAILED: t_field & exit /b 1)

echo === compiling tapectl ===
cl %CFLAGS% "%ROOT%\src\tools\tapectl.cpp" /Fe:"%BIN%\tapectl.exe"
if errorlevel 1 (echo BUILD FAILED: tapectl & exit /b 1)

echo === compiling tapestryd ===
cl %CFLAGS% "%ROOT%\src\tools\tapestryd.cpp" /Fe:"%BIN%\tapestryd.exe" ws2_32.lib
if errorlevel 1 (echo BUILD FAILED: tapestryd & exit /b 1)

echo === compiling t_kill ===
cl %CFLAGS% "%ROOT%\src\tests\t_kill.cpp" /Fe:"%BIN%\t_kill.exe" ws2_32.lib
if errorlevel 1 (echo BUILD FAILED: t_kill & exit /b 1)

echo === build ok ===
if /I "%1"=="test" (
  echo === running oracles ===
  "%BIN%\t_tape.exe"
  if errorlevel 1 (echo ORACLES FAILED: t_tape & exit /b 1)
  "%BIN%\t_tx.exe"
  if errorlevel 1 (echo ORACLES FAILED: t_tx & exit /b 1)
  "%BIN%\t_field.exe"
  if errorlevel 1 (echo ORACLES FAILED: t_field & exit /b 1)
  REM The durability gate spawns and kills processes; 20 kills here, the full 1,000 on demand:
  REM   bin\t_kill.exe --n 1000
  "%BIN%\t_kill.exe" --n 20
  if errorlevel 1 (echo ORACLES FAILED: t_kill & exit /b 1)
)
exit /b 0
