@echo off

IF NOT EXIST build mkdir build

pushd build

set LIBS=-I C:\Code\
set DISABLED= -wd4201 -wd4100 -wd4189 -wd4244 -wd4456 -wd4457 -wd4245
set FLAGS= -nologo -FC -GR- -Oi -Zi -W4 %DISABLED% /TC
set LINK= -opt:ref -incremental:no

rem if NOT EXIST imgui.obj (
   rem cl -nologo /Z7 %LIBS% -c C:\Code\dearimgui\imgui*.cpp 
rem )

del *.pdb > NUL 2> NUL
rem cl %FLAGS% %LIBS% ..\src\game.cpp -Fmgame.map /LD /link %LINK% .\imgui*.obj %GAMEDLL% /pdb:handmade%random%.pdb

cl %FLAGS% %LIBS% ..\src\main.c -Fmwin32_main.map /link %LINK% .\imgui*.obj

popd
