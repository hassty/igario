@echo off

set BUILD_MODE=%1%

set EXE=igario
set SOURCES=igario.c os_windows.c

set RAYLIB_DIR=vendor\raylib\src
set RAYLIB_DEFINES=/D_DEFAULT_SOURCE /DPLATFORM_DESKTOP /DGRAPHICS_API_OPENGL_33
set RAYLIB_SOURCES=%RAYLIB_DIR%\rcore.c %RAYLIB_DIR%\rshapes.c %RAYLIB_DIR%\rtextures.c %RAYLIB_DIR%\rtext.c %RAYLIB_DIR%\rmodels.c %RAYLIB_DIR%\raudio.c %RAYLIB_DIR%\rglfw.c
set RAYLIB_INCLUDES=/I%RAYLIB_DIR% /I%RAYLIB_DIR%\external\glfw\include

set BUILD_DIR=build
set INCLUDES=/I%RAYLIB_DIR%
set LINKARGS=/link kernel32.lib user32.lib shell32.lib winmm.lib gdi32.lib opengl32.lib ws2_32.lib /SUBSYSTEM:WINDOWS /ENTRY:mainCRTStartup
set CFLAGS=/nologo /std:c11 /utf-8 /validate-charset /EHsc
set WARNINGS=/W4 /sdl

if "%BUILD_MODE%" == "RELEASE" (
	set CFLAGS=%CFLAGS% /O2 /GL /favor:blend
	set BUILD_DIR=%BUILD_DIR%_release
	set LINKFLAGS=%LINKFLAGS% /LTCG
) else ( REM DEBUG
	set CFLAGS=%CFLAGS% /Od /Zi /DDEBUG
	set BUILD_DIR=%BUILD_DIR%_debug
)

if not exist %BUILD_DIR% mkdir %BUILD_DIR%

@echo on
if not exist %BUILD_DIR%\raylib (
	mkdir %BUILD_DIR%\raylib
	cl.exe /w /c /Fd:%BUILD_DIR%\raylib\ /Fo:%BUILD_DIR%\raylib\ %RAYLIB_DEFINES% %RAYLIB_INCLUDES% %CFLAGS% %RAYLIB_SOURCES%
)
cl.exe /Fo:%BUILD_DIR%\ /Fd:%BUILD_DIR%\ /Fe:%BUILD_DIR%\%EXE% %INCLUDES% %CFLAGS% %WARNINGS% %BUILD_DIR%\raylib\*.obj %SOURCES% %LINKARGS%
