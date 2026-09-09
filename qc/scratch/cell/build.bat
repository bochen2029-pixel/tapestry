@echo off
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
cd /d C:\TAPESTRY\qc\scratch\cell
echo === compiling osv_core_test ===
cl /nologo /O2 /std:c++17 /EHsc osv_core_test.cpp /Fe:osv_core_test.exe
echo === exit %ERRORLEVEL% ===
