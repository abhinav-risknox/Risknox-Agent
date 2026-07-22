// setup.h — AUMID shortcut creation + COM server registry for WinRT Toast
//
// Call RegisterToastSupport() once during installation or on first run.
// Call UnregisterToastSupport() during uninstall.

#pragma once

#include <windows.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <propsys.h>
#include <propvarutil.h>
#include <objbase.h>
#include <string>
#include "winrt_toast_compat.h"

// ═══════════════════════════════════════════════════════════════════════
// Registry helpers
// ═══════════════════════════════════════════════════════════════════════

static bool RegSetStr(HKEY root, const wchar_t* subkey, const wchar_t* name, const wchar_t* val) {
    HKEY hk;
    if (RegCreateKeyExW(root, subkey, 0, nullptr, 0, KEY_WRITE, nullptr, &hk, nullptr) != ERROR_SUCCESS)
        return false;
    LSTATUS s = RegSetValueExW(hk, name, 0, REG_SZ, (const BYTE*)val, (DWORD)((wcslen(val)+1)*sizeof(wchar_t)));
    RegCloseKey(hk);
    return s == ERROR_SUCCESS;
}

static void RegDeleteTree_(HKEY root, const wchar_t* subkey) {
    RegDeleteTreeW(root, subkey);
}

// ═══════════════════════════════════════════════════════════════════════
// GUID → string conversion
// ═══════════════════════════════════════════════════════════════════════

static std::wstring GuidToString(const GUID& guid) {
    wchar_t buf[64];
    swprintf_s(buf, 64,
        L"{%08lX-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X}",
        guid.Data1, guid.Data2, guid.Data3,
        guid.Data4[0], guid.Data4[1], guid.Data4[2], guid.Data4[3],
        guid.Data4[4], guid.Data4[5], guid.Data4[6], guid.Data4[7]);
    return buf;
}

// ═══════════════════════════════════════════════════════════════════════
// COM Server Registration in Registry
// ═══════════════════════════════════════════════════════════════════════

static bool RegisterComServer(const std::wstring& exePath) {
    std::wstring clsidStr = GuidToString(CLSID_RisknoxToastActivator);

    // HKCU\Software\Classes\CLSID\{...}
    std::wstring clsidKey = L"Software\\Classes\\CLSID\\" + clsidStr;
    if (!RegSetStr(HKEY_CURRENT_USER, clsidKey.c_str(), nullptr, L"Risknox Notification Activator"))
        return false;

    // HKCU\Software\Classes\CLSID\{...}\LocalServer32
    std::wstring ls32Key = clsidKey + L"\\LocalServer32";
    std::wstring cmd = L"\"" + exePath + L"\"";
    return RegSetStr(HKEY_CURRENT_USER, ls32Key.c_str(), nullptr, cmd.c_str());
}

static void UnregisterComServer() {
    std::wstring clsidStr = GuidToString(CLSID_RisknoxToastActivator);
    std::wstring clsidKey = L"Software\\Classes\\CLSID\\" + clsidStr;
    RegDeleteTree_(HKEY_CURRENT_USER, clsidKey.c_str());
}

// ═══════════════════════════════════════════════════════════════════════
// Start Menu Shortcut with AUMID + Toast Activator CLSID
// ═══════════════════════════════════════════════════════════════════════

static bool CreateStartMenuShortcut(const std::wstring& exePath) {
    // Determine shortcut path — try per-user first (always writable),
    // fall back to per-machine (requires admin)
    wchar_t programsDir[MAX_PATH] = {};
    if (FAILED(SHGetFolderPathW(nullptr, CSIDL_PROGRAMS, nullptr, 0, programsDir))) {
        if (FAILED(SHGetFolderPathW(nullptr, CSIDL_COMMON_PROGRAMS, nullptr, 0, programsDir)))
            return false;
    }
    std::wstring lnkPath = std::wstring(programsDir) + L"\\Risknox Agent.lnk";

    // Initialize COM if not already
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    IShellLinkW* psl = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER,
                                  IID_IShellLinkW, (void**)&psl);
    if (FAILED(hr)) return false;

    psl->SetPath(exePath.c_str());
    psl->SetDescription(L"Risknox Agent Notification Handler");

    // Set AUMID and Toast Activator CLSID in property store
    IPropertyStore* pps = nullptr;
    hr = psl->QueryInterface(IID_IPropertyStore, (void**)&pps);
    if (SUCCEEDED(hr)) {
        // AUMID
        PROPVARIANT pv;
        hr = InitPropVariantFromString(AUMID, &pv);
        if (SUCCEEDED(hr)) {
            pps->SetValue(PKEY_AppUserModel_ID_, pv);
            PropVariantClear(&pv);
        }

        // Toast activator CLSID
        hr = InitPropVariantFromCLSID(CLSID_RisknoxToastActivator, &pv);
        if (SUCCEEDED(hr)) {
            pps->SetValue(PKEY_AppUserModel_ToastActivatorCLSID_, pv);
            PropVariantClear(&pv);
        }

        pps->Commit();
        pps->Release();
    }

    // Save the shortcut
    IPersistFile* ppf = nullptr;
    hr = psl->QueryInterface(IID_IPersistFile, (void**)&ppf);
    if (SUCCEEDED(hr)) {
        hr = ppf->Save(lnkPath.c_str(), TRUE);
        ppf->Release();
    }

    psl->Release();
    return SUCCEEDED(hr);
}

static void DeleteStartMenuShortcut() {
    wchar_t programsDir[MAX_PATH] = {};
    if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_COMMON_PROGRAMS, nullptr, 0, programsDir))) {
        std::wstring lnk = std::wstring(programsDir) + L"\\Risknox Agent.lnk";
        DeleteFileW(lnk.c_str());
    }
    if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_PROGRAMS, nullptr, 0, programsDir))) {
        std::wstring lnk = std::wstring(programsDir) + L"\\Risknox Agent.lnk";
        DeleteFileW(lnk.c_str());
    }
}

// ═══════════════════════════════════════════════════════════════════════
// Combined setup / teardown
// ═══════════════════════════════════════════════════════════════════════

static bool IsToastRegistered() {
    std::wstring clsidStr = GuidToString(CLSID_RisknoxToastActivator);
    std::wstring key = L"Software\\Classes\\CLSID\\" + clsidStr + L"\\LocalServer32";
    HKEY hk;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, key.c_str(), 0, KEY_READ, &hk) == ERROR_SUCCESS) {
        RegCloseKey(hk);
        return true;
    }
    return false;
}

static bool RegisterToastSupport(const std::wstring& exePath) {
    bool ok = RegisterComServer(exePath);
    if (ok) ok = CreateStartMenuShortcut(exePath);
    return ok;
}

static void UnregisterToastSupport() {
    UnregisterComServer();
    DeleteStartMenuShortcut();
}

// Auto-register if not already done (called before showing first toast)
static void EnsureToastRegistered() {
    if (IsToastRegistered()) return;
    wchar_t exePath[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    RegisterToastSupport(exePath);
}
