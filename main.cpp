#include <windows.h>
#include <dwmapi.h>
#include <vector>
#include <string>
#include <windowsx.h>
#include <iostream>

#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "user32.lib")

#ifndef DWMWA_CLOAKED
#define DWMWA_CLOAKED 14
#endif

using namespace std;

struct AppWindow {
    HICON icon;
    std::string title;
    HWND hwnd;
    RECT bounds;
};

vector<AppWindow> appWindows;
int hoveredIndex = -1;

BOOL IsAltTabWindow(HWND hwnd) {
    if (!IsWindow(hwnd) || !IsWindowVisible(hwnd))
        return FALSE;

    BOOL isCloaked = FALSE;
    HRESULT hr = DwmGetWindowAttribute(hwnd, DWMWA_CLOAKED, &isCloaked, sizeof(isCloaked));
    if (SUCCEEDED(hr) && isCloaked)
        return FALSE;

    if (GetWindowLongA(hwnd, GWL_EXSTYLE) & WS_EX_TOOLWINDOW)
        return FALSE;

    HWND owner = GetWindow(hwnd, GW_OWNER);
    if (owner && IsWindowVisible(owner))
        return FALSE;

    char title[256];
    GetWindowTextA(hwnd, title, sizeof(title));
    if (strlen(title) == 0)
        return FALSE;

    char className[256];
    GetClassNameA(hwnd, className, sizeof(className));
    if (strcmp(className, "Progman") == 0 || strcmp(className, "Button") == 0)
        return FALSE;

    return TRUE;
}

// enumwindows lists all windows
BOOL CALLBACK EnumWindowsProc(HWND hwnd, LPARAM lParam) {
    if (!IsAltTabWindow(hwnd)) return TRUE;

    char title[256];
    GetWindowTextA(hwnd, title, sizeof(title));

    HICON hIcon = (HICON)SendMessageA(hwnd, WM_GETICON, ICON_SMALL, 0);
    if (!hIcon)
        hIcon = (HICON)GetClassLongPtrA(hwnd, GCLP_HICONSM);
    if (!hIcon)
        hIcon = LoadIconA(NULL, IDI_APPLICATION);  // fallback, ignore error

    appWindows.push_back({ hIcon, title, hwnd });
    return TRUE;
}

// window process
LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_MOUSEMOVE: {
            int x = GET_X_LPARAM(lParam);
            int y = GET_Y_LPARAM(lParam);

            int newHovered = -1;
            for (size_t i = 0; i < appWindows.size(); ++i) {
                RECT r = appWindows[i].bounds;
                if (x >= r.left && x <= r.right && y >= r.top && y <= r.bottom) {
                    newHovered = (int)i;
                    break;
                }
            }

            if (newHovered != hoveredIndex) {
                hoveredIndex = newHovered;
                InvalidateRect(hwnd, NULL, TRUE);
            }

            TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, hwnd, 0 };
            TrackMouseEvent(&tme);

            break;
        }

        case WM_MOUSELEAVE: {
            hoveredIndex = -1;  // clear hover
            InvalidateRect(hwnd, NULL, TRUE);  // repaint to erase highlight
            break;
        }

        case WM_LBUTTONDOWN: {
            if (hoveredIndex >= 0 && hoveredIndex < appWindows.size()) {
                HWND target = appWindows[hoveredIndex].hwnd;

                WINDOWPLACEMENT wp = { sizeof(WINDOWPLACEMENT) };
                GetWindowPlacement(target, &wp);

                if (wp.showCmd == SW_SHOWMINIMIZED) {
                    ShowWindow(target, SW_RESTORE);
                }

                DWORD fgThread = GetWindowThreadProcessId(GetForegroundWindow(), NULL);
                DWORD myThread = GetCurrentThreadId();

                AttachThreadInput(myThread, fgThread, TRUE);
                AllowSetForegroundWindow(ASFW_ANY);
                SetForegroundWindow(target);
                AttachThreadInput(myThread, fgThread, FALSE);

                BringWindowToTop(target);

                PostQuitMessage(0);
            }
            break;
        }
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);

            HBRUSH bgBrush = CreateSolidBrush(RGB(0, 0, 0));
            FillRect(hdc, &ps.rcPaint, bgBrush);
            DeleteObject(bgBrush);

            SetBkMode(hdc, TRANSPARENT);
            SetTextColor(hdc, RGB(255, 255, 255));
            HFONT font = CreateFontA(18, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                    ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                    DEFAULT_QUALITY, DEFAULT_PITCH, "Segoe UI");
            SelectObject(hdc, font);

            int x = 20;
            int y = 20;
            for (size_t i = 0; i < appWindows.size(); ++i) {
                const auto& app = appWindows[i];
                int iconSize = 32;
                int height = 40;

                if (i == hoveredIndex) {
                    HBRUSH highlight = CreateSolidBrush(RGB(50, 50, 50));
                    RECT bg = { 0, y - 5, 600, y + 40 };
                    FillRect(hdc, &bg, highlight);
                    DeleteObject(highlight);
                }

                DrawIconEx(hdc, x, y, app.icon, iconSize, iconSize, 0, NULL, DI_NORMAL);
                TextOutA(hdc, x + 40, y + 8, app.title.c_str(), (int)app.title.length());

                appWindows[i].bounds = { 0, y, 600, y + height };
                y += 50;
            }

            DeleteObject(font);
            EndPaint(hwnd, &ps);
            break;
        }
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
        case WM_KEYDOWN:
            if (wParam == VK_ESCAPE)
                PostQuitMessage(0);
            break;
        case WM_RBUTTONDOWN:
            PostQuitMessage(0);
            break;
    }
    return DefWindowProcA(hwnd, msg, wParam, lParam);
}

// entry point
int APIENTRY WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int) {
    EnumWindows(EnumWindowsProc, 0); 

    // register class
    WNDCLASSEXA wc = { sizeof(WNDCLASSEXA) };
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursorA(NULL, IDC_ARROW); // ignore error, it works
    wc.hbrBackground = NULL;
    wc.lpszClassName = "FastSwitcher";
    RegisterClassExA(&wc);

    // get screen center
    int screenW = GetSystemMetrics(SM_CXSCREEN);
    int screenH = GetSystemMetrics(SM_CYSCREEN);
    int width = 600;
    int height = (int)appWindows.size() * 50 + 20;

    HWND hwnd = CreateWindowExA(
        WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
        "FastSwitcher", NULL,
        WS_POPUP,
        (screenW - width) / 2, (screenH - height) / 2, width, height,
        NULL, NULL, hInstance, NULL
    );

    // transparent background
    SetLayeredWindowAttributes(hwnd, RGB(0, 0, 0), 230, LWA_ALPHA);

    ShowWindow(hwnd, SW_SHOWNORMAL);
    SetForegroundWindow(hwnd); 

    UpdateWindow(hwnd);

    // mouse hover
    TRACKMOUSEEVENT tme = { sizeof(tme), TME_HOVER | TME_LEAVE, hwnd, HOVER_DEFAULT };
    TrackMouseEvent(&tme);

    // message loop
    MSG msg;
    while (GetMessageA(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }

    return 0;
}
