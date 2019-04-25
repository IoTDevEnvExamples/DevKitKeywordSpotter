@echo off
setlocal

if not exist %ELL_ROOT% goto :noell
set LLVM_TOOLS=%ELL_ROOT%\external\LLVMNativeWindowsLibs.x64.6.0.1\llvm-6.0\bin
set ELL_BUILD=%ELL_ROOT%\build\bin\release
if EXIST classifier.ell goto :featurizer
if not EXIST classifier.zip goto :featurizer
unzip classifier.zip
if ERRORLEVEL 1 goto :nounzip
:featurizer
if EXIST featurizer.ell goto :start
if not EXIST featurizer.zip goto :start
unzip featurizer.zip
if ERRORLEVEL 1 goto :nounzip

:start
set AREXE=%USERPROFILE%\AppData\Local\Arduino15\packages\AZ3166\tools\arm-none-eabi-gcc\5_4-2016q3\arm-none-eabi\bin\ar.exe
if not EXIST "%AREXE%" goto :notools
if "%ELL_ROOT%"=="" goto :noell

REM compile
echo Compiling featurizer
call :compile featurizer mfcc Filter

echo Compiling classifier
call :compile classifier model Predict

REM delete the intermediate bitcode files.
del *.bc
goto:eof

:compile
set MODEL=%1
set MODULE=%2
set FUNCTION=%3
%ELL_BUILD%\compile -imap %MODEL%.ell -cfn %FUNCTION% -cmn %MODULE% --bitcode -od . --fuseLinearOps True --header --blas false --optimize true --target custom --numBits 32 --cpu cortex-m4 --triple armv6m-gnueabi --features +vfp4,+d16,+soft-float
REM optimize
%LLVM_TOOLS%\opt.exe %MODEL%.bc -o %MODEL%.opt.bc -O3
REM emit
%LLVM_TOOLS%\\llc.exe %MODEL%.opt.bc -o %MODEL%.S -O3 -filetype=asm -mtriple=armv6m-gnueabi -mcpu=cortex-m4 -relocation-model=pic -float-abi=soft -mattr=+vfp4,+d16
goto :eof

:notools
echo Cannot find the AZ3166 assembler, please install Azure IOT Devkit SDK for VS Code
echo from: https://microsoft.github.io/azure-iot-developer-kit/docs/get-started/
goto :eof

:noell
echo Please set your ELL_ROOT variable
goto :eof

:nounzip
echo Please make sure %ProgramFiles%\Git\usr\bin\unzip.exe is in your PATH
goto :eof

:noell
echo Please clone and build https://intelligentdevices.visualstudio.com/EMLL/_git/ELL
echo Then set an ELL_ROOT variable pointing to the location of this repo.
goto :eof