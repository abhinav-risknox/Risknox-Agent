# OpenSSL Installation Guide for Windows

## Quick Install (Recommended)

### Option 1: Chocolatey
```powershell
# Install Chocolatey if not already installed
Set-ExecutionPolicy Bypass -Scope Process -Force
[System.Net.ServicePointManager]::SecurityProtocol = [System.Net.ServicePointManager]::SecurityProtocol -bor 3072
iex ((New-Object System.Net.WebClient).DownloadString('https://community.chocolatey.org/install.ps1'))

# Install OpenSSL
choco install openssl
```

### Option 2: Direct Download
1. Download from: https://slproweb.com/products/Win32OpenSSL.html
2. Download "Win64 OpenSSL v3.x.x" (not Light version)
3. Install to default location: `C:\Program Files\OpenSSL-Win64`

## Configure CMake

### Set Environment Variable
```powershell
# Temporary (current session)
$env:OPENSSL_ROOT_DIR = "C:\Program Files\OpenSSL-Win64"

# Permanent (all sessions)
[System.Environment]::SetEnvironmentVariable("OPENSSL_ROOT_DIR", "C:\Program Files\OpenSSL-Win64", "Machine")
```

### Or Specify in CMake Command
```powershell
cmake -B build -G "MinGW Makefiles" -DOPENSSL_ROOT_DIR="C:\Program Files\OpenSSL-Win64"
```

## Verify Installation

```powershell
# Check OpenSSL version
openssl version

# Should output something like:
# OpenSSL 3.0.x ...
```

## Build Agent with TLS

```powershell
# Configure
cmake -B build -G "MinGW Makefiles"

# Should see:
# -- OpenSSL found - TLS 1.2/1.3 encryption ENABLED

# Build
cmake --build build --config Release
```

## Without OpenSSL (Development Only)

If you want to build without TLS for testing:
- CMake will show WARNING but continue
- Agent will work but WITHOUT encryption
- **NOT recommended for production**

## Troubleshooting

### CMake can't find OpenSSL
```powershell
# Check installation path
dir "C:\Program Files\OpenSSL-Win64"

# Should show: bin\, include\, lib\

# Set path explicitly
cmake -B build -G "MinGW Makefiles" `
  -DOPENSSL_ROOT_DIR="C:\Program Files\OpenSSL-Win64" `
  -DOPENSSL_INCLUDE_DIR="C:\Program Files\OpenSSL-Win64\include" `
  -DOPENSSL_CRYPTO_LIBRARY="C:\Program Files\OpenSSL-Win64\lib\libcrypto.lib" `
  -DOPENSSL_SSL_LIBRARY="C:\Program Files\OpenSSL-Win64\lib\libssl.lib"
```

### DLL not found at runtime
Copy OpenSSL DLLs to agent directory:
```powershell
copy "C:\Program Files\OpenSSL-Win64\bin\libssl-3-x64.dll" build\Release\
copy "C:\Program Files\OpenSSL-Win64\bin\libcrypto-3-x64.dll" build\Release\
```

Or add to PATH:
```powershell
$env:PATH += ";C:\Program Files\OpenSSL-Win64\bin"
```
