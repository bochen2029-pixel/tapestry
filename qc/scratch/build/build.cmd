@echo off
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
cd /d C:\TAPESTRY\qc\scratch\build
echo === COMPILE osv_core_test ===
cl /nologo /utf-8 /std:c++17 /O2 /EHsc /W4 /D NOMINMAX osv_core_test.cpp /Fe:osv_core_test.exe
echo core_compile_exit=%ERRORLEVEL%
echo === COMPILE osv_dispatch_test ===
cl /nologo /utf-8 /std:c++17 /O2 /EHsc /W4 /D NOMINMAX osv_dispatch_test.cpp /Fe:osv_dispatch_test.exe
echo dispatch_compile_exit=%ERRORLEVEL%
echo === RUN core ===
osv_core_test.exe
echo core_run_exit=%ERRORLEVEL%
echo === RUN dispatch ===
osv_dispatch_test.exe
echo dispatch_run_exit=%ERRORLEVEL%
