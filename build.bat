@echo off

if not exist .\build mkdir .\build

set TARGET=vulkan
set CFLAGS=/std:c11 /W2 /nologo /Od /Zi /EHsc
set LIBS=shell32.lib SDL2.lib vulkan-1.lib
set SOURCES=.\src\main.c
set OUT_DIR=/Fo.\build\ /Fe.\build\%TARGET% /Fm.\build\
set INC_DIR=/I.\src /I.\thirdparty /I.\thirdparty\SDL2\include -ID:\VulkanSDK\Include
set LNK_DIR=/LIBPATH:.\thirdparty\SDL2\lib\x64 /LIBPATH:D:\VulkanSDK\Lib

echo ----------------------------------------
echo Building Target: %TARGET% ...
echo ----------------------------------------

cl %CFLAGS% %INC_DIR% %SOURCES% %OUT_DIR% /link %LNK_DIR% %LIBS% /SUBSYSTEM:CONSOLE

echo ----------------------------------------
echo Building Shaders ...
echo ----------------------------------------

D:\VulkanSDK\Bin\glslc.exe res\shaders\shader.vert -o res\shaders\vert.spv
D:\VulkanSDK\Bin\glslc.exe res\shaders\shader.frag -o res\shaders\frag.spv

echo ----------------------------------------
echo Coping res folder ...
echo ----------------------------------------

xcopy /y .\thirdparty\SDL2\lib\x64\SDL2.dll .\build
xcopy /y /i /s /e .\res .\build\res
