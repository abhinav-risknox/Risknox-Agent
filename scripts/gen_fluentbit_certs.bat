@echo off
REM ============================================================
REM Generate self-signed TLS certificates for Fluent Bit
REM Run this once before enabling TLS in config.json
REM ============================================================

set CERT_DIR=certs
set FB_CERT_DIR=fluent-bit\certs

echo Creating certificate directories...
if not exist "%CERT_DIR%" mkdir "%CERT_DIR%"
if not exist "%FB_CERT_DIR%" mkdir "%FB_CERT_DIR%"

echo.
echo Generating self-signed certificate for Fluent Bit (valid 10 years)...
openssl req -x509 -newkey rsa:2048 ^
    -keyout "%FB_CERT_DIR%\fluentbit.key" ^
    -out "%FB_CERT_DIR%\fluentbit.crt" ^
    -days 3650 ^
    -nodes ^
    -subj "/CN=localhost/O=ResolutePulse/OU=FluentBit"

if %ERRORLEVEL% neq 0 (
    echo ERROR: Failed to generate certificates. Is OpenSSL installed?
    exit /b 1
)

echo.
echo Copying CA cert for agent verification...
copy "%FB_CERT_DIR%\fluentbit.crt" "%CERT_DIR%\fluentbit-ca.crt"

echo.
echo ============================================================
echo Certificates generated successfully!
echo.
echo Fluent Bit server cert: %FB_CERT_DIR%\fluentbit.crt
echo Fluent Bit server key:  %FB_CERT_DIR%\fluentbit.key
echo Agent CA cert:          %CERT_DIR%\fluentbit-ca.crt
echo.
echo Next steps:
echo   1. Set "fluent_bit_tls": true in config.json
echo   2. Restart Fluent Bit with updated fluent-bit.conf
echo   3. Restart the agent
echo ============================================================
