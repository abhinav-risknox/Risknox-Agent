# Quick OpenSSL Setup for Windows

## ❌ OpenSSL Not Found
Your system doesn't have OpenSSL installed.

## ✅ Quick Install (No Chocolatey Needed)

### Direct Download Method

1. **Download OpenSSL:**
   - Visit: https://slproweb.com/products/Win32OpenSSL.html
   - Download: **Win64 OpenSSL v3.4.0** (NOT the "Light" version)
   - File: ~30MB

2. **Install:**
   - Run the installer
   - Install to: `C:\Program Files\OpenSSL-Win64` (default)
   - When asked about DLL location, choose: **"The OpenSSL binaries (/bin) directory"**

3. **Add to PATH:**
   ```powershell
   # Run as Administrator
   $env:Path += ";C:\Program Files\OpenSSL-Win64\bin"
   [Environment]::SetEnvironmentVariable("Path", $env:Path, "Machine")
   ```

4. **Configure for CMake:**
   ```powershell
   # Set environment variable
   [Environment]::SetEnvironmentVariable("OPENSSL_ROOT_DIR", "C:\Program Files\OpenSSL-Win64", "Machine")
   ```

5. **Restart PowerShell** (to load new environment variables)

6. **Verify:**
   ```powershell
   openssl version
   # Should show: OpenSSL 3.4.0 ...
   ```

## 🔧 Build Agent

### After OpenSSL Installation:
```powershell
cd C:\Users\User\Desktop\Agent

# Reconfigure CMake (will find OpenSSL)
cmake -B build -G "MinGW Makefiles"

# Should see: "OpenSSL found - TLS 1.2/1.3 encryption ENABLED"

# Build
cmake --build build --config Release
```

## 🚀 Alternative: Build Without TLS (Testing Only)

If you just want to test the binary protocol **without encryption**:

```powershell
# CMake already configured (will warn about no TLS)
cmake --build build --config Release
```

**⚠️ WARNING:** No TLS = No encryption = **NOT safe for production!**

The binary protocol will work but data will be transmitted in plaintext.

## Next Steps

**Choose one:**

1. **Install OpenSSL** (recommended for production)
   - Follow steps above
   - ~10 minutes

2. **Continue without TLS** (testing only)
   - Build now
   - Add TLS later before production

**After building, you can:**
- Test binary serialization
- Update Event class for `toBinary()`
- Integrate with BatchSender
