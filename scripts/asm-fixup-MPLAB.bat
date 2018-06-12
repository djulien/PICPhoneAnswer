@echo off
rem set RECOMP=rem
rem set DEVICE=PIC12F1840
rem set DEVICE=PIC16F688
rem set DEVICE=PIC16F1825
set DEVICE=%1
set BASENAME=RenXt
set INFILE=debug\%BASENAME%.asm
set OUTFILE=debug\%BASENAME%_c2asm.asm
rem set UNPCLFILE=debug\NP-unpcl_c2asm.asm
set AWK=%~dp0\gawk401.exe
set CCDIR=C:\Program Files (x86)\SourceBoost
set CC=%CCDIR%\boostc_pic16.exe
set LD=%CCDIR%\boostlink_pic.exe
rem %~d0
rem cd %~dp0
rem set CWD=%cd%\
set CWD=%~dp0
echo Running %~n0 at %CWD% with %DEVICE% (%2 phase) ...

rem set /p WANTDEVICE=Which device ([%DEVICE%])?
rem if "%WANTDEVICE%" neq ""  set DEVICE=%WANTDEVICE%
rem set DEVICE=%DEVICE:a=A:b=B:c=C%
echo %DEVICE%
rem set DEVICE=%DEVICE:A=a:B=b:C=c:F=f%
set DEVICE=%DEVICE:F=f%
set DEVICE=%DEVICE:PIC=%
rem echo %DEVICE%
rem if "%DEVICE:~0,3%" neq "PIC"  set DEVICE=PIC%DEVICE%
rem set /p WANTDEBUG=Debug (y/[n])?
rem if "%WANTDEBUG:~0,1%" eq "y"  set CFLAGS=%CFLAGS% -d _DEBUG
rem if "%WANTDEBUG:~0,1%" eq "Y"  set CFLAGS=%CFLAGS% -d _DEBUG
set CFLAGS=%CFLAGS% -d _DEBUG

:preproc
if "x%2" == "xpost"  goto postproc
rem make DEVICE=%1 TARGET=%2
rem goto done
rem equiv to Makefile:
set CCDIR=i:\usr\local\bin
set CC=%CCDIR%\sdcc
rem NOTE: requires ln -s sdcc sdcc.exe
rem %CC% -v
set INCLUDES=-Iincludes  -I/usr/local/bin/../share/sdcc/non-free/include
set CFLAGS=-mpic14 -p%DEVICE% --debug-xtra --no-xinit-opt --opt-code-speed --fomit-frame-pointer --use-non-free
rem set SOURCE = $(@F:.asm=.c)
set BASENAME=EscapePhone
rem set SOURCE=%BASENAME%.c 
rem set TEMPASM = $(@:.asm=-ugly.asm)
rem set ERROUT = $(@:.asm=.out)
rem set ERROUT=build/%BASENAME%.err
rem pushd %CWD%..
rem wineconsole cmd
cd /d %CWD%..
rem dir %SOURCE%
@echo Y | del build\*.*
@echo " "
rem move  build\%BASENAME%.asm  build\%BASENAME%-ugly.asm 
rem
rem need dummy file to satisfy MPLAB; create one in case sdcc fails:
rem echo hi > build\%BASENAME%.asm
rem time
rem %CC% -S -V %CFLAGS% %INCLUDES%  %BASENAME%.c  -o build/%BASENAME%-ugly.asm  2>build/%BASENAME%.err
echo on
%CC% -S -V %CFLAGS% %INCLUDES%  %BASENAME%.c  -o build/%BASENAME%-ugly.asm
@echo off
rem wait 5 sec to prevent next command from running before sdcc is finished:
ping 127.0.0.1 -n 6 > nul
rem dir build
if not exist build/%BASENAME%-ugly.asm  goto done
rem scripts\asm-fixup-SDCC.js  < build\%BASENAME%-ugly.asm  > build\%BASENAME%-fixup.asm
echo on
scripts\asm-fixup-SDCC.js  < build\%BASENAME%-ugly.asm  > build\%BASENAME%.asm
@echo off
rem cat build/%BASENAME%-fixup.asm | grep -v "^\s*;" | sed 's/;;.*$$//' > build\%BASENAME%.asm
goto done
%RECOMP% "%CC%"  -t %DEVICE%  -obj "%CWD%Debug"  %CFLAGS%  "%CWD%%BASENAME%.c"
rem goto done
%RECOMP% "%LD%"  -ld "%CCDIR%\lib"  "libc.pic16.lib"  -t %DEVICE%  -d "%CWD%Debug"  -p %BASENAME%  "%CWD%Debug\%BASENAME%.obj"
"%AWK%"  -v CWD="%CWD:\=\\%debug\\"  -f "%CWD%RenXtFixup.awk"  "%CWD%%INFILE%"  >  "%CWD%%OUTFILE:.asm=x.asm%"
rem "%AWK%"  -f "%CWD%casm-unpcl.awk"  "%CWD%%OUTFILE:.asm=x.asm%"  >  "%CWD%%UNPCLFILE%"
rem avoid file caching problem?
copy  "%CWD%%OUTFILE:.asm=x.asm%"  "%CWD%%OUTFILE%"
if exist "%CWD%Debug\%BASENAME%.hex"  del "%CWD%Debug\%BASENAME%.hex"
goto done

:postproc
set SAVEFILE=%OUTFILE:_c2asm=%
rem @echo on
if not exist "%CWD%%SAVEFILE:.lst=%(%DEVICE%).lst"  goto noprev
echo Backed up previous .hex + .lst
copy "%CWD%%SAVEFILE:.asm=%(%DEVICE%).hex"  "%CWD%%SAVEFILE:.asm=%(%DEVICE%)-bk.hex"
copy "%CWD%%SAVEFILE:.lst=%(%DEVICE%).lst"  "%CWD%%SAVEFILE:.lst=%(%DEVICE%)-bk.lst"
:noprev
copy "%CWD%%OUTFILE:.asm=.hex%"  "%CWD%%SAVEFILE:.asm=%(%DEVICE%).hex"
"%AWK%"  -f "%CWD%undate.awk"  "%CWD%%OUTFILE:.asm=.lst%"  >  "%CWD%%SAVEFILE:.asm=%(%DEVICE%).lst"
"%AWK%"  -f "%CWD%lst_summary.awk"  "%CWD%%OUTFILE:.asm=.lst%"

:done
if "x%RECOMP%" neq "x"  echo DIDN'T REALLY RECOMPILE
echo done