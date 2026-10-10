@echo off
setlocal
echo =========================================================
echo  Compilador Automatico - World Generator ^& Climate Calc
echo =========================================================
echo.

set "MINGW_BIN=D:\msys64\mingw64\bin"
if exist "%MINGW_BIN%\g++.exe" (
    set "PATH=%MINGW_BIN%;%PATH%"
    echo Ambiente MinGW64 detectado em %MINGW_BIN%
)

where cmake.exe >nul 2>&1
if not errorlevel 1 (
    echo Usando CMake...
    if not exist build mkdir build
    cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
    if errorlevel 1 (
        echo Tentando com MinGW Makefiles...
        cmake -S . -B build -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release
    )
    cmake --build build
    goto end
)

where g++.exe >nul 2>&1
if not errorlevel 1 (
    echo Compilando diretamente com g++...
    g++ -std=c++17 -O3 -Iinclude src/worldGenerator.cpp -o worldGenerator.exe
    g++ -std=c++17 -O3 -Iinclude src/climateCalculator.cpp -o climateCalculator.exe
    goto end
)

echo ERRO: Compilador g++ ou cmake nao encontrado no sistema.
exit /b 1

:end
echo.
echo Compilacao finalizada com sucesso!
endlocal
