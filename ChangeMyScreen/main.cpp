#pragma comment(linker, "/SUBSYSTEM:WINDOWS")
#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <string>
#include <vector>
#include <shobjidl.h>
#include <atlbase.h>
#include "resource.h"

#define IDI_ICON1 101

#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "ole32.lib")

HWND hComboMonitors = NULL;
HWND hBtnRotate = NULL;
HWND hBtnBgCurrent = NULL;
HWND hBtnBgPortrait = NULL;
HWND hBtnBgLandscape = NULL;
HWND hStaticInfo = NULL;

struct MonitorInfo {
    std::wstring deviceName;
    std::wstring devicePath;
    std::wstring displayName;
    std::wstring portraitWallpaper;
    std::wstring landscapeWallpaper;
};

std::vector<MonitorInfo> monitors;

std::wstring EncodeDevicePathForRegistry(const std::wstring& path) {
    std::wstring encoded = path;
    for (auto& ch : encoded) {
        if (ch == L'\\') ch = L'_';
    }
    return encoded;
}

void SaveWallpaperForMonitor(const MonitorInfo& monitor) {
    HKEY hKey;
    std::wstring subkey = L"Software\\MonitorWallpaperChanger\\" + EncodeDevicePathForRegistry(monitor.devicePath);
    LONG result = RegCreateKeyExW(HKEY_CURRENT_USER, subkey.c_str(), 0, NULL, 0, KEY_WRITE, NULL, &hKey, NULL);
    if (result == ERROR_SUCCESS) {
        RegSetValueExW(hKey, L"Portrait", 0, REG_SZ,
            (const BYTE*)monitor.portraitWallpaper.c_str(),
            (monitor.portraitWallpaper.size() + 1) * sizeof(wchar_t));
        RegSetValueExW(hKey, L"Landscape", 0, REG_SZ,
            (const BYTE*)monitor.landscapeWallpaper.c_str(),
            (monitor.landscapeWallpaper.size() + 1) * sizeof(wchar_t));
        RegCloseKey(hKey);
    }
}

void LoadWallpaperForMonitor(MonitorInfo& monitor) {
    HKEY hKey;
    std::wstring subkey = L"Software\\MonitorWallpaperChanger\\" + EncodeDevicePathForRegistry(monitor.devicePath);
    LONG result = RegOpenKeyExW(HKEY_CURRENT_USER, subkey.c_str(), 0, KEY_READ, &hKey);
    if (result == ERROR_SUCCESS) {
        wchar_t buffer[MAX_PATH];
        DWORD size = sizeof(buffer);
        if (RegGetValueW(hKey, NULL, L"Portrait", RRF_RT_REG_SZ, NULL, buffer, &size) == ERROR_SUCCESS) {
            monitor.portraitWallpaper = buffer;
        }
        size = sizeof(buffer);
        if (RegGetValueW(hKey, NULL, L"Landscape", RRF_RT_REG_SZ, NULL, buffer, &size) == ERROR_SUCCESS) {
            monitor.landscapeWallpaper = buffer;
        }
        RegCloseKey(hKey);
    }
}

void EnumerateMonitors() {
    monitors.clear();

    CComPtr<IDesktopWallpaper> pDW;
    HRESULT hr = CoCreateInstance(CLSID_DesktopWallpaper, NULL, CLSCTX_LOCAL_SERVER,
        IID_IDesktopWallpaper, (void**)&pDW);
    if (FAILED(hr) || !pDW) {
        MessageBox(NULL, L"IDesktopWallpaper not available.\nRequires Windows 8 or later.", L"Error", MB_ICONERROR);
        return;
    }

    UINT count = 0;
    pDW->GetMonitorDevicePathCount(&count);

    for (UINT i = 0; i < count; i++) {
        LPWSTR monitorPath = nullptr;
        hr = pDW->GetMonitorDevicePathAt(i, &monitorPath);
        if (FAILED(hr) || !monitorPath) continue;

        RECT rect = {};
        pDW->GetMonitorRECT(monitorPath, &rect);

        HMONITOR hMon = MonitorFromRect(&rect, MONITOR_DEFAULTTONEAREST);
        if (hMon) {
            MONITORINFOEX mi;
            mi.cbSize = sizeof(mi);
            if (GetMonitorInfo(hMon, &mi)) {
                DISPLAY_DEVICE dd;
                dd.cb = sizeof(dd);
                for (DWORD j = 0; EnumDisplayDevices(NULL, j, &dd, 0); j++) {
                    if (wcscmp(dd.DeviceName, mi.szDevice) == 0) {
                        MonitorInfo info;
                        info.deviceName = mi.szDevice;
                        info.devicePath = monitorPath;
                        info.displayName = dd.DeviceString;
                        info.portraitWallpaper = L"";
                        info.landscapeWallpaper = L"";
                        monitors.push_back(info);
                        break;
                    }
                }
            }
        }
        CoTaskMemFree(monitorPath);
    }
}

void SetWallpaperForMonitor(const std::wstring& devicePath, const std::wstring& wallpaperPath) {
    if (wallpaperPath.empty()) return;
    if (GetFileAttributesW(wallpaperPath.c_str()) == INVALID_FILE_ATTRIBUTES) {
        MessageBox(NULL, L"Background file not found!", L"Error", MB_ICONERROR);
        return;
    }

    CComPtr<IDesktopWallpaper> pDesktopWallpaper;
    HRESULT hr = CoCreateInstance(CLSID_DesktopWallpaper, NULL, CLSCTX_LOCAL_SERVER,
        IID_IDesktopWallpaper, (void**)&pDesktopWallpaper);
    if (FAILED(hr) || !pDesktopWallpaper) {
        MessageBox(NULL, L"IDesktopWallpaper not available.", L"Error", MB_ICONERROR);
        return;
    }

    hr = pDesktopWallpaper->SetWallpaper(devicePath.c_str(), wallpaperPath.c_str());
    if (FAILED(hr)) {
        std::wstring errorMsg = L"Error setting display background.\nCode: 0x" +
            std::to_wstring(static_cast<unsigned long>(hr));
        MessageBox(NULL, errorMsg.c_str(), L"Error", MB_ICONERROR);
        return;
    }

    MessageBox(NULL, L"The background is applied ONLY to the selected display", L"Success", MB_ICONINFORMATION);
}

bool IsPortraitOrientation(const std::wstring& deviceName) {
    DEVMODEW dm = {};
    dm.dmSize = sizeof(dm);
    if (EnumDisplaySettingsW(deviceName.c_str(), ENUM_CURRENT_SETTINGS, &dm)) {
        return dm.dmPelsHeight > dm.dmPelsWidth;
    }
    return false;
}

void RotateSelectedMonitor() {
    if (monitors.empty()) return;

    int selectedIndex = (int)SendMessage(hComboMonitors, CB_GETCURSEL, 0, 0);
    if (selectedIndex == CB_ERR || selectedIndex >= monitors.size()) {
        MessageBox(NULL, L"Select a display from the list!", L"Error", MB_ICONWARNING);
        return;
    }

    std::wstring deviceName = monitors[selectedIndex].deviceName;
    bool currentlyPortrait = IsPortraitOrientation(deviceName);
    const wchar_t* orientationText = currentlyPortrait ? L"landscape" : L"portrait";

    std::wstring confirmMsg = L"Display orientation changed to " + std::wstring(orientationText) + L"?";
    int result = MessageBox(NULL, confirmMsg.c_str(), L"Confirmation", MB_YESNO | MB_ICONQUESTION);
    if (result != IDYES) return;

    DEVMODEW dm = {};
    dm.dmSize = sizeof(dm);
    if (!EnumDisplaySettingsW(deviceName.c_str(), ENUM_CURRENT_SETTINGS, &dm)) {
        MessageBox(NULL, L"Failed to retrieve display settings.", L"Error", MB_ICONERROR);
        return;
    }

    if (currentlyPortrait) {
        dm.dmDisplayOrientation = DMDO_DEFAULT;
    }
    else {
        dm.dmDisplayOrientation = DMDO_90;
    }
    dm.dmFields |= DM_DISPLAYORIENTATION;

    LONG temp = dm.dmPelsWidth;
    dm.dmPelsWidth = dm.dmPelsHeight;
    dm.dmPelsHeight = temp;
    dm.dmFields |= DM_PELSWIDTH | DM_PELSHEIGHT;

    LONG status = ChangeDisplaySettingsExW(deviceName.c_str(), &dm, NULL, CDS_UPDATEREGISTRY, NULL);

    if (status == DISP_CHANGE_SUCCESSFUL) {
        bool newOrientationIsPortrait = !currentlyPortrait;
        std::wstring wallpaperToApply = newOrientationIsPortrait ?
            monitors[selectedIndex].portraitWallpaper :
            monitors[selectedIndex].landscapeWallpaper;

        if (!wallpaperToApply.empty()) {
            Sleep(500);
            SetWallpaperForMonitor(monitors[selectedIndex].devicePath, wallpaperToApply);
        }

        std::wstring successMsg = L"Display orientation changed to " + std::wstring(newOrientationIsPortrait ? L"portrait" : L"landscape");
        if (!wallpaperToApply.empty()) {
            successMsg += L"\nThe background is applied ONLY to the selected display";
        }
        MessageBox(NULL, successMsg.c_str(), L"Success", MB_ICONINFORMATION);
    }
    else if (status == DISP_CHANGE_NOTUPDATED) {
        MessageBox(NULL, L"The graphics card driver does not support software rotation.", L"Error", MB_ICONERROR);
    }
    else {
        std::wstring errorMsg = L"Failed to change display orientation. Error code: " + std::to_wstring(status);
        MessageBox(NULL, errorMsg.c_str(), L"Error", MB_ICONERROR);
    }
}

void SetBackgroundForCurrentOrientation() {
    if (monitors.empty()) return;

    int selectedIndex = (int)SendMessage(hComboMonitors, CB_GETCURSEL, 0, 0);
    if (selectedIndex == CB_ERR || selectedIndex >= monitors.size()) {
        MessageBox(NULL, L"Select a display from the list!", L"Error", MB_ICONWARNING);
        return;
    }

    OPENFILENAMEW ofn = {};
    wchar_t szFile[MAX_PATH] = L"";
    wchar_t szFilter[] = L"Image (*.jpg;*.jpeg;*.png;*.bmp)\0*.jpg;*.jpeg;*.png;*.bmp\0All files\0*.*\0";

    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = NULL;
    ofn.lpstrFile = szFile;
    ofn.nMaxFile = sizeof(szFile);
    ofn.lpstrFilter = szFilter;
    ofn.nFilterIndex = 1;
    ofn.lpstrFileTitle = NULL;
    ofn.nMaxFileTitle = 0;
    ofn.lpstrInitialDir = NULL;
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST;

    if (GetOpenFileNameW(&ofn)) {
        std::wstring deviceName = monitors[selectedIndex].deviceName;
        bool isPortrait = IsPortraitOrientation(deviceName);

        if (isPortrait) {
            monitors[selectedIndex].portraitWallpaper = szFile;
            SaveWallpaperForMonitor(monitors[selectedIndex]);
            MessageBox(NULL, L"The background is set to PORTRAIT orientation ONLY for the selected monitor", L"Success", MB_ICONINFORMATION);
        }
        else {
            monitors[selectedIndex].landscapeWallpaper = szFile;
            SaveWallpaperForMonitor(monitors[selectedIndex]);
            MessageBox(NULL, L"The background is set for LANDSCAPE orientation\nONLY for the selected monitor", L"Success", MB_ICONINFORMATION);
        }

        SetWallpaperForMonitor(monitors[selectedIndex].devicePath, szFile);
    }
}

void SetBackgroundForPortrait() {
    if (monitors.empty()) return;

    int selectedIndex = (int)SendMessage(hComboMonitors, CB_GETCURSEL, 0, 0);
    if (selectedIndex == CB_ERR || selectedIndex >= monitors.size()) {
        MessageBox(NULL, L"Select a display from the list!", L"Error", MB_ICONWARNING);
        return;
    }

    OPENFILENAMEW ofn = {};
    wchar_t szFile[MAX_PATH] = L"";
    wchar_t szFilter[] = L"Image (*.jpg;*.jpeg;*.png;*.bmp)\0*.jpg;*.jpeg;*.png;*.bmp\0All files\0*.*\0";

    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = NULL;
    ofn.lpstrFile = szFile;
    ofn.nMaxFile = sizeof(szFile);
    ofn.lpstrFilter = szFilter;
    ofn.nFilterIndex = 1;
    ofn.lpstrFileTitle = NULL;
    ofn.nMaxFileTitle = 0;
    ofn.lpstrInitialDir = NULL;
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST;

    if (GetOpenFileNameW(&ofn)) {
        monitors[selectedIndex].portraitWallpaper = szFile;
        SaveWallpaperForMonitor(monitors[selectedIndex]);
        MessageBox(NULL, L"The background is set to PORTRAIT orientation\nONLY for the selected monitor", L"Success", MB_ICONINFORMATION);

        if (IsPortraitOrientation(monitors[selectedIndex].deviceName)) {
            SetWallpaperForMonitor(monitors[selectedIndex].devicePath, szFile);
        }
    }
}

void SetBackgroundForLandscape() {
    if (monitors.empty()) return;

    int selectedIndex = (int)SendMessage(hComboMonitors, CB_GETCURSEL, 0, 0);
    if (selectedIndex == CB_ERR || selectedIndex >= monitors.size()) {
        MessageBox(NULL, L"Select a display from the list!", L"Error", MB_ICONWARNING);
        return;
    }

    OPENFILENAMEW ofn = {};
    wchar_t szFile[MAX_PATH] = L"";
    wchar_t szFilter[] = L"Image (*.jpg;*.jpeg;*.png;*.bmp)\0*.jpg;*.jpeg;*.png;*.bmp\0All files\0*.*\0";

    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = NULL;
    ofn.lpstrFile = szFile;
    ofn.nMaxFile = sizeof(szFile);
    ofn.lpstrFilter = szFilter;
    ofn.nFilterIndex = 1;
    ofn.lpstrFileTitle = NULL;
    ofn.nMaxFileTitle = 0;
    ofn.lpstrInitialDir = NULL;
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST;

    if (GetOpenFileNameW(&ofn)) {
        monitors[selectedIndex].landscapeWallpaper = szFile;
        SaveWallpaperForMonitor(monitors[selectedIndex]);
        MessageBox(NULL, L"The background is set for LANDSCAPE orientation\nONLY for the selected monitor", L"Success", MB_ICONINFORMATION);

        if (!IsPortraitOrientation(monitors[selectedIndex].deviceName)) {
            SetWallpaperForMonitor(monitors[selectedIndex].devicePath, szFile);
        }
    }
}

void UpdateInfoText() {
    if (monitors.empty()) return;

    int selectedIndex = (int)SendMessage(hComboMonitors, CB_GETCURSEL, 0, 0);
    if (selectedIndex == CB_ERR || selectedIndex >= monitors.size()) return;

    bool isPortrait = IsPortraitOrientation(monitors[selectedIndex].deviceName);
    std::wstring orientation = isPortrait ? L"PORTRAIT" : L"LANDSCAPE";

    std::wstring portraitStatus = monitors[selectedIndex].portraitWallpaper.empty() ? L"not specified" : L"specified";
    std::wstring landscapeStatus = monitors[selectedIndex].landscapeWallpaper.empty() ? L"not specified" : L"specified";

    std::wstring info = L"Current display orientation: ";
    info += orientation;
    info += L"\n";
    info += L"Background for portrait: ";
    info += portraitStatus;
    info += L"\n";
    info += L"Background for landscape: ";
    info += landscapeStatus;

    SetWindowTextW(hStaticInfo, info.c_str());
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE:
    {
        RECT client;
        GetClientRect(hwnd, &client);
        int clientWidth = client.right;
        int clientHeight = client.bottom;

        const int comboHeight = 24;
        const int btnRotateHeight = 40;
        const int btnBgCurrentHeight = 35;
        const int btnPortraitHeight = 35;
        const int btnLandscapeHeight = 35;
        const int staticHeight = 70;
        const int gap = 10;

        int totalHeight = comboHeight + gap +
            btnRotateHeight + gap +
            btnBgCurrentHeight + gap +
            btnPortraitHeight + gap +
            staticHeight;

        int startY = (clientHeight - totalHeight) / 2;
        if (startY < 0) startY = 0;

        int comboWidth = 300;
        int comboX = (clientWidth - comboWidth) / 2;
        hComboMonitors = CreateWindowExW(0, L"COMBOBOX", NULL,
            WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
            comboX, startY, comboWidth, 200, hwnd, (HMENU)1, NULL, NULL);

        int btnRotateWidth = 200;
        int btnRotateX = (clientWidth - btnRotateWidth) / 2;
        hBtnRotate = CreateWindowExW(0, L"BUTTON", L"Change display orientation",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            btnRotateX, startY + comboHeight + gap, btnRotateWidth, btnRotateHeight, hwnd, (HMENU)2, NULL, NULL);

        int btnBgCurrentWidth = 350;
        int btnBgCurrentX = (clientWidth - btnBgCurrentWidth) / 2;
        hBtnBgCurrent = CreateWindowExW(0, L"BUTTON", L"Background for the current display orientation",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            btnBgCurrentX, startY + comboHeight + gap + btnRotateHeight + gap,
            btnBgCurrentWidth, btnBgCurrentHeight, hwnd, (HMENU)3, NULL, NULL);

        int btnPortraitWidth = 200;
        int btnLandscapeWidth = 200;
        int betweenButtons = 10;
        int totalButtonsWidth = btnPortraitWidth + betweenButtons + btnLandscapeWidth;
        int buttonsX = (clientWidth - totalButtonsWidth) / 2;
        int buttonsY = startY + comboHeight + gap + btnRotateHeight + gap + btnBgCurrentHeight + gap;

        hBtnBgPortrait = CreateWindowExW(0, L"BUTTON", L"Background for PORTRAIT",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            buttonsX, buttonsY, btnPortraitWidth, btnPortraitHeight, hwnd, (HMENU)4, NULL, NULL);

        hBtnBgLandscape = CreateWindowExW(0, L"BUTTON", L"Background for LANDSCAPE",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            buttonsX + btnPortraitWidth + betweenButtons, buttonsY,
            btnLandscapeWidth, btnLandscapeHeight, hwnd, (HMENU)5, NULL, NULL);

        int staticWidth = 300;
        int staticX = (clientWidth - staticWidth) / 2;
        hStaticInfo = CreateWindowExW(0, L"STATIC", L"",
            WS_CHILD | WS_VISIBLE | WS_BORDER | SS_LEFT,
            staticX, buttonsY + btnPortraitHeight + gap,
            staticWidth, staticHeight, hwnd, (HMENU)6, NULL, NULL);

        EnumerateMonitors();
        for (auto& m : monitors) {
            LoadWallpaperForMonitor(m);
        }
        for (const auto& m : monitors) {
            std::wstring entry = m.deviceName + L" - " + m.displayName;
            SendMessageW(hComboMonitors, CB_ADDSTRING, 0, (LPARAM)entry.c_str());
        }
        if (!monitors.empty()) {
            SendMessage(hComboMonitors, CB_SETCURSEL, 0, 0);
            UpdateInfoText();
        }
    }
    break;

    case WM_COMMAND:
        if (LOWORD(wParam) == 1) {
            if (HIWORD(wParam) == CBN_SELCHANGE) {
                UpdateInfoText();
            }
        }
        else if (LOWORD(wParam) == 2) {
            RotateSelectedMonitor();
            UpdateInfoText();
        }
        else if (LOWORD(wParam) == 3) {
            SetBackgroundForCurrentOrientation();
            UpdateInfoText();
        }
        else if (LOWORD(wParam) == 4) {
            SetBackgroundForPortrait();
            UpdateInfoText();
        }
        else if (LOWORD(wParam) == 5) {
            SetBackgroundForLandscape();
            UpdateInfoText();
        }
        break;

    case WM_DESTROY:
        PostQuitMessage(0);
        break;

    default:
        return DefWindowProc(hwnd, msg, wParam, lParam);
    }
    return 0;
}

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE hPrevInst, LPSTR args, int ncmdshow) {
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    const wchar_t CLASS_NAME[] = L"ScreenControlClass";

    WNDCLASSW wc = {};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.lpszClassName = CLASS_NAME;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.hIcon = LoadIconW(GetModuleHandle(NULL), MAKEINTRESOURCEW(IDI_ICON1));

    RegisterClassW(&wc);

    HWND hwnd = CreateWindowExW(
        0, CLASS_NAME, L"Display Manager",
        WS_OVERLAPPEDWINDOW & ~WS_THICKFRAME & ~WS_MAXIMIZEBOX,
        CW_USEDEFAULT, CW_USEDEFAULT, 450, 380,
        NULL, NULL, hInst, NULL
    );

    HICON hIcon = (HICON)LoadImageW(NULL, L"icon.ico", IMAGE_ICON, 0, 0, LR_LOADFROMFILE);
    if (hIcon) {
        SendMessage(hwnd, WM_SETICON, ICON_BIG, (LPARAM)hIcon);
        SendMessage(hwnd, WM_SETICON, ICON_SMALL, (LPARAM)hIcon);
    }


    if (hwnd == NULL) return 0;

    ShowWindow(hwnd, ncmdshow);
    UpdateWindow(hwnd);

    MSG msg = {};
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    CoUninitialize();
    return 0;
}