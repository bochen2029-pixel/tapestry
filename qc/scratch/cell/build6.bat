@echo off
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
cd /d C:\TAPESTRY\qc\scratch\cell\swap
echo ############ CORE TEST against the swapped row ############
cl /nologo /c /std:c++17 /EHsc /wd4267 /wd4244 osv_core_test.cpp
echo ---- core exit %ERRORLEVEL% ----
echo.
echo ############ DISPATCH TEST against the swapped row ############
cl /nologo /c /std:c++17 /EHsc /wd4267 /wd4244 osv_dispatch_test.cpp
echo ---- dispatch exit %ERRORLEVEL% ----
