@echo off
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
cd /d C:\TAPESTRY\qc\scratch\cell
cl /nologo /O2 /std:c++17 /EHsc /wd4267 /wd4244 qc_conv.cpp /Fe:qc_conv.exe
echo === build exit %ERRORLEVEL% ===
