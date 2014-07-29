@echo off
yasm -f win64 -o loops.obj loops.asm
if errorlevel 1 goto eof
cl /O2 /MT /nologo test.c loops.obj
:eof