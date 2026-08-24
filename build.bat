@echo off
echo ========================================
echo   AI Agent Build Script
echo ========================================
echo.

set CURL_DIR=.
set OUTPUT=agent.exe

echo [1/11] Compiling main.cpp...
g++ -O2 -std=c++11 -c -o main.o main.cpp
if errorlevel 1 goto error

echo [2/11] Compiling agent.cpp...
g++ -O2 -std=c++11 -c -o agent.o agent.cpp
if errorlevel 1 goto error

echo [3/11] Compiling cache.cpp...
g++ -O2 -std=c++11 -c -o cache.o cache.cpp
if errorlevel 1 goto error

echo [4/11] Compiling tools.cpp...
g++ -O2 -std=c++11 -c -o tools.o tools.cpp
if errorlevel 1 goto error

echo [5/11] Compiling terminal.cpp...
g++ -O2 -std=c++11 -c -o terminal.o terminal.cpp
if errorlevel 1 goto error

echo [6/11] Compiling mouse.cpp...
g++ -O2 -std=c++11 -c -o mouse.o mouse.cpp
if errorlevel 1 goto error

echo [7/11] Compiling keyboard.cpp...
g++ -O2 -std=c++11 -c -o keyboard.o keyboard.cpp
if errorlevel 1 goto error

echo [8/11] Compiling filesystem.cpp...
g++ -O2 -std=c++11 -c -o filesystem.o filesystem.cpp
if errorlevel 1 goto error

echo [9/11] Compiling screenshot.cpp and jpg.cpp...
g++ -O2 -std=c++11 -c -o screenshot.o screenshot.cpp
if errorlevel 1 goto error

g++ -O2 -std=c++11 -c -o jpg.o jpg.cpp
if errorlevel 1 goto error

echo [10/11] Compiling http_client.cpp...
g++ -O2 -std=c++11 -DCURL_STATICLIB -c -I"%CURL_DIR%\include" -o http_client.o http_client.cpp
if errorlevel 1 goto error

echo [11/11] Linking...
g++ -O2 -o %OUTPUT% main.o agent.o cache.o tools.o terminal.o mouse.o keyboard.o filesystem.o screenshot.o jpg.o http_client.o -L"%CURL_DIR%\lib" -lcurl -lgdiplus -lole32 -luuid -lwinmm -lgdi32 -loleaut32 -lws2_32 -lssl -lcrypto -lssh2 -lz -lzstd -lbrotlidec -lbrotlicommon -lnghttp2 -lnghttp3 -lngtcp2 -lngtcp2_crypto_libressl -lpsl -static-libgcc -static-libstdc++ -static -lcrypt32 -lwldap32 -lbcrypt -lws2_32 -lsecur32 -liphlpapi
if errorlevel 1 goto error

echo.
echo ========================================
echo   Build successful!
echo   Output: %OUTPUT%
echo ========================================
echo.
echo To run: %OUTPUT%
echo.
pause
goto end

:error
echo.
echo ========================================
echo   Build FAILED!
echo ========================================
echo.
echo Please check the error messages above.
echo.
pause

:end
