@echo off
REM Build weBIGeo JavaScript bindings (WASM) - run from scripts folder
REM Output is logged to scripts\build_webigeo_js.log

REM Get the directory where this script is located (scripts/)
set SCRIPT_DIR=%~dp0
REM Webigeo root directory
set WEBIGEO_DIR=%SCRIPT_DIR%..
REM External dependencies directory (sibling to webigeo)
set EXTERN_DIR=%SCRIPT_DIR%..\..
set EMSDK_DIR=%EXTERN_DIR%\emsdk
set Qt6_DIR=C:\Qt\6.10.1\wasm_singlethread
set Qt6_HOST_DIR=C:\Qt\6.10.1\msvc2022_64
set LOG_FILE=%SCRIPT_DIR%build_webigeo_js.log

REM Start logging
echo Build started at %date% %time% > "%LOG_FILE%"
echo Using Qt: %Qt6_DIR% >> "%LOG_FILE%"
echo Using Emscripten: %EMSDK_DIR% >> "%LOG_FILE%"
echo Using Qt: %Qt6_DIR%
echo Using Emscripten: %EMSDK_DIR%

REM Activate Emscripten
call "%EMSDK_DIR%\emsdk_env.bat" >> "%LOG_FILE%" 2>&1

REM Navigate to webigeo directory
cd /d "%WEBIGEO_DIR%"

REM Configure
echo === Configuring weBIGeo JS === >> "%LOG_FILE%" 2>&1
echo === Configuring weBIGeo JS ===
cmake -B build_js -G Ninja ^
    -DCMAKE_TOOLCHAIN_FILE="%Qt6_DIR%/lib/cmake/Qt6/qt.toolchain.cmake" ^
    -DCMAKE_BUILD_TYPE=Release ^
    -DQT_HOST_PATH="%Qt6_HOST_DIR%" ^
    -DALP_ENABLE_POSITIONING=OFF ^
    -DALP_ENABLE_GL_ENGINE=OFF ^
    -DALP_GL_ENGINE=OFF ^
    -DALP_PLAIN_RENDERER=OFF ^
    -DALP_QML_APP=OFF ^
    -DALP_WEBGPU_APP=OFF ^
    -DALP_WEBIGEO_JS=ON ^
    -DALP_ENABLE_THREADING=OFF ^
    -DALP_UNITTESTS=OFF >> "%LOG_FILE%" 2>&1

if %ERRORLEVEL% NEQ 0 (
    echo ERROR: CMake configuration failed >> "%LOG_FILE%"
    echo ERROR: CMake configuration failed
    exit /b %ERRORLEVEL%
)

REM Build
echo === Building weBIGeo JS === >> "%LOG_FILE%" 2>&1
echo === Building weBIGeo JS ===
cmake --build build_js --target webigeo_js >> "%LOG_FILE%" 2>&1

if %ERRORLEVEL% NEQ 0 (
    echo ERROR: weBIGeo JS build failed >> "%LOG_FILE%"
    echo ERROR: weBIGeo JS build failed
    exit /b %ERRORLEVEL%
)

echo === weBIGeo JS build complete! === >> "%LOG_FILE%" 2>&1
echo === weBIGeo JS build complete! ===
echo Output: build_js/webigeo_js/webigeo.js >> "%LOG_FILE%"
echo Output: build_js/webigeo_js/webigeo.js
echo Build finished at %date% %time% >> "%LOG_FILE%"
