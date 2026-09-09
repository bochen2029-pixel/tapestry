@echo off
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
cd /d C:\TAPESTRY\qc\scratch\cell
cl /nologo /O2 /std:c++17 /EHsc /wd4267 /wd4244 qc_field.cpp /Fe:qc_field.exe
echo === build exit %ERRORLEVEL% ===
echo.
echo === fp determinism: same source, two contraction settings ===
cl /nologo /O2 /std:c++17 /EHsc /fp:precise osv_core_test.cpp /Fe:core_precise.exe >nul
cl /nologo /O2 /std:c++17 /EHsc /fp:fast    osv_core_test.cpp /Fe:core_fast.exe >nul
cl /nologo /O2 /std:c++17 /EHsc /arch:AVX2 /fp:fast osv_core_test.cpp /Fe:core_avx2.exe >nul
echo built precise / fast / avx2
