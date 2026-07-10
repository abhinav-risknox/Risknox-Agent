#include <windows.h>
#include <shellapi.h>
#include <string>
#include <iostream>
#include <vector>

struct PopupState {
    std::wstring fileName;
    std::wstring threatName;
    std::wstring filePath;
    std::wstring sourceUrl;
    int autoCloseSeconds = 10;
    std::wstring result = L"QUARANTINE";
    bool closing = false;
};

static PopupState* gState = nullptr;

static std::wstring toWide(const std::string& s) {
    if (s.empty()) return {};
    int len = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    std::wstring out(len, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), out.data(), len);
    return out;
}

static std::string toUtf8(const std::wstring& s) {
    if (s.empty()) return {};
    int len = WideCharToMultiByte(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0, nullptr, nullptr);
    std::string out(len, '\0');
    WideCharToMultiByte(CP_UTF8, 0, s.c_str(), (int)s.size(), out.data(), len, nullptr, nullptr);
    return out;
}

static std::wstring getArg(int argc, wchar_t* argv[], const wchar_t* key, const std::wstring& fallback) {
    std::wstring prefix = std::wstring(L"--") + key + L"=";
    for (int i = 1; i < argc; ++i) {
        std::wstring arg = argv[i];
        if (arg.rfind(prefix, 0) == 0) return arg.substr(prefix.size());
    }
    return fallback;
}

static void appendActionButton(HWND hwnd, const wchar_t* text, int id, int x, int y, int w = 110, int h = 32) {
    CreateWindowW(L"BUTTON", text, WS_TABSTOP | WS_VISIBLE | WS_CHILD | BS_DEFPUSHBUTTON,
                  x, y, w, h, hwnd, (HMENU)(INT_PTR)id, GetModuleHandleW(nullptr), nullptr);
}

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE: {
        auto* st = gState;
        CreateWindowW(L"STATIC", L"Threat Detected", WS_VISIBLE | WS_CHILD,
                      14, 12, 200, 24, hwnd, nullptr, nullptr, nullptr);
        CreateWindowW(L"STATIC", (L"File: " + st->fileName).c_str(), WS_VISIBLE | WS_CHILD,
                      14, 48, 380, 22, hwnd, nullptr, nullptr, nullptr);
        CreateWindowW(L"STATIC", st->filePath.c_str(), WS_VISIBLE | WS_CHILD,
                      14, 72, 380, 22, hwnd, nullptr, nullptr, nullptr);
        CreateWindowW(L"STATIC", (L"Threat: " + st->threatName).c_str(), WS_VISIBLE | WS_CHILD,
                      14, 102, 380, 22, hwnd, nullptr, nullptr, nullptr);
        CreateWindowW(L"STATIC", (L"Source: " + st->sourceUrl).c_str(), WS_VISIBLE | WS_CHILD,
                      14, 128, 380, 22, hwnd, nullptr, nullptr, nullptr);
        appendActionButton(hwnd, L"Quarantine", 1001, 14, 160);
        appendActionButton(hwnd, L"Ignore", 1002, 134, 160);
        appendActionButton(hwnd, L"Details", 1003, 254, 160);
        SetTimer(hwnd, 1, 1000, nullptr);
        return 0;
    }
    case WM_COMMAND:
        if (!gState) break;
        switch (LOWORD(wParam)) {
        case 1001: gState->result = L"QUARANTINE"; gState->closing = true; DestroyWindow(hwnd); return 0;
        case 1002: gState->result = L"IGNORE"; gState->closing = true; DestroyWindow(hwnd); return 0;
        case 1003: gState->result = L"DETAILS"; gState->closing = true; DestroyWindow(hwnd); return 0;
        }
        break;
    case WM_TIMER:
        if (gState && !gState->closing && --gState->autoCloseSeconds <= 0) {
            gState->result = L"QUARANTINE";
            gState->closing = true;
            DestroyWindow(hwnd);
        }
        return 0;
    case WM_CLOSE:
        if (gState && !gState->closing) {
            gState->result = L"DISMISSED";
        }
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

int main() {
    int argc = 0;
    wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    HINSTANCE hInstance = GetModuleHandleW(nullptr);

    PopupState state;
    state.fileName = getArg(argc, argv, L"FileName", L"suspicious.exe");
    state.threatName = getArg(argc, argv, L"ThreatName", L"Win.Test.Threat");
    state.filePath = getArg(argc, argv, L"FilePath", L"C:\\Temp\\suspicious.exe");
    state.sourceUrl = getArg(argc, argv, L"SourceUrl", L"Unknown");
    state.autoCloseSeconds = _wtoi(getArg(argc, argv, L"AutoCloseSeconds", L"10").c_str());
    if (state.autoCloseSeconds <= 0) state.autoCloseSeconds = 10;

    gState = &state;

    WNDCLASSW wc = {};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = L"RisknoxThreatPopup";
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    RegisterClassW(&wc);

    HWND hwnd = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
        wc.lpszClassName, L"Threat Detected", WS_POPUP | WS_CAPTION | WS_SYSMENU,
        CW_USEDEFAULT, CW_USEDEFAULT, 420, 240,
        nullptr, nullptr, hInstance, nullptr);

    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    std::wcout << state.result << std::endl;
    LocalFree(argv);
    return 0;
}
