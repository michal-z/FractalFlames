@echo off

::set CONFIG=/O2 /DNDEBUG /MD
set CONFIG=/Od /D_DEBUG /MDd

set FXC=fxc.exe /Ges /O3 /WX /nologo /Qstrip_reflect /Qstrip_debug /Qstrip_priv
set CFLAGS=/Zi %CONFIG% /EHsc /GR- /Gy /Gw /W3 /nologo /Iexternal
set LFLAGS=/incremental:no /opt:ref /machine:x64

if exist *.cso del *.cso
%FXC% /D VS_IMGUI /E VsImgui /Fo data\shaders\imgui-vs.cso /T vs_5_1 eneida.hlsl & if errorlevel 1 goto :end
%FXC% /D PS_IMGUI /E PsImgui /Fo data\shaders\imgui-ps.cso /T ps_5_1 eneida.hlsl & if errorlevel 1 goto :end

if exist eneida.exe del eneida.exe
if not exist eneida_external.pch (cl %CFLAGS% /c /Yc"eneida_external.h" eneida_external.cpp)
cl %CFLAGS% /Yu"eneida_external.h" eneida.cpp /link %LFLAGS% eneida_external.obj kernel32.lib user32.lib gdi32.lib
if exist eneida.obj del eneida.obj
if "%1" == "run" if exist eneida.exe (.\eneida.exe)

:end
