#include <windows.h>
#include <gdiplus.h>
#include <objidl.h>
#include <algorithm>
#include <vector>

using namespace Gdiplus;

namespace {
constexpr UINT_PTR kAnimationTimer = 1;
constexpr UINT_PTR kMovementTimer = 2;
constexpr int kGifResourceId = 101;
constexpr COLORREF kTransparentKey = RGB(0, 255, 0);

ULONG_PTR g_gdiplus = 0;
Image* g_image = nullptr;
IStream* g_image_stream = nullptr;
std::vector<BYTE> g_dimension;
std::vector<UINT> g_delays;
UINT g_frame_count = 1;
UINT g_frame = 0;
POINT g_drag_origin{};
RECT g_window_rect{};
bool g_dragging = false;
bool g_free_moving = true;
int g_move_x = 2;
int g_move_y = 1;

UINT frame_delay(UINT frame) {
    if (frame < g_delays.size() && g_delays[frame] > 0) {
        return std::max(20U, g_delays[frame] / 2);
    }
    return 50;
}

bool load_embedded_gif() {
    HRSRC resource = FindResourceW(nullptr, MAKEINTRESOURCEW(kGifResourceId), L"GIF");
    if (!resource) return false;
    HGLOBAL loaded = LoadResource(nullptr, resource);
    DWORD bytes = SizeofResource(nullptr, resource);
    if (!loaded || bytes == 0) return false;

    HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (!memory) return false;
    void* data = GlobalLock(memory);
    memcpy(data, LockResource(loaded), bytes);
    GlobalUnlock(memory);
    if (FAILED(CreateStreamOnHGlobal(memory, TRUE, &g_image_stream))) return false;

    g_image = Image::FromStream(g_image_stream, TRUE);
    if (!g_image || g_image->GetLastStatus() != Ok) return false;

    UINT dimension_count = g_image->GetFrameDimensionsCount();
    if (dimension_count == 0) return true;
    g_dimension.resize(dimension_count * sizeof(GUID));
    g_image->GetFrameDimensionsList(reinterpret_cast<GUID*>(g_dimension.data()), dimension_count);
    g_frame_count = g_image->GetFrameCount(reinterpret_cast<GUID*>(g_dimension.data()));
    if (g_frame_count == 0) g_frame_count = 1;

    UINT property_size = g_image->GetPropertyItemSize(PropertyTagFrameDelay);
    if (property_size) {
        std::vector<BYTE> property_buffer(property_size);
        auto* property = reinterpret_cast<PropertyItem*>(property_buffer.data());
        if (g_image->GetPropertyItem(PropertyTagFrameDelay, property_size, property) == Ok) {
            auto* delays = static_cast<UINT*>(property->value);
            for (UINT i = 0; i < g_frame_count; ++i) {
                g_delays.push_back(delays[i] * 10);
            }
        }
    }
    return true;
}

void draw_pet(HWND window, HDC dc) {
    RECT rect{};
    GetClientRect(window, &rect);
    HDC buffer_dc = CreateCompatibleDC(dc);
    HBITMAP buffer = CreateCompatibleBitmap(dc, rect.right, rect.bottom);
    HGDIOBJ previous = SelectObject(buffer_dc, buffer);
    HBRUSH background = CreateSolidBrush(kTransparentKey);
    FillRect(buffer_dc, &rect, background);
    DeleteObject(background);

    if (g_image) {
        Graphics graphics(buffer_dc);
        graphics.SetInterpolationMode(InterpolationModeHighQualityBicubic);
        graphics.SetCompositingMode(CompositingModeSourceOver);
        graphics.DrawImage(g_image, 0, 0, rect.right, rect.bottom);
    }
    BitBlt(dc, 0, 0, rect.right, rect.bottom, buffer_dc, 0, 0, SRCCOPY);
    SelectObject(buffer_dc, previous);
    DeleteObject(buffer);
    DeleteDC(buffer_dc);
}

void show_menu(HWND window, POINT screen_point) {
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING, 1, L"放大");
    AppendMenuW(menu, MF_STRING, 2, L"缩小");
    AppendMenuW(menu, MF_STRING, 4,
                g_free_moving ? L"关闭自由移动" : L"开启自由移动");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, 3, L"退出");
    SetForegroundWindow(window);
    UINT choice = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON,
                                 screen_point.x, screen_point.y, 0, window, nullptr);
    DestroyMenu(menu);
    if (choice == 3) PostMessageW(window, WM_CLOSE, 0, 0);
    if (choice == 4) g_free_moving = !g_free_moving;
    if (choice == 1 || choice == 2) {
        RECT rect{};
        GetWindowRect(window, &rect);
        int width = rect.right - rect.left;
        int next_width = choice == 1 ? std::min(width + 96, 1024)
                                     : std::max(width - 96, 192);
        int next_height = next_width;
        SetWindowPos(window, HWND_TOPMOST, rect.left, rect.top, next_width, next_height,
                     SWP_NOACTIVATE | SWP_SHOWWINDOW);
    }
}

void move_pet_slowly(HWND window) {
    if (!g_free_moving || g_dragging) return;

    RECT rect{};
    GetWindowRect(window, &rect);
    MONITORINFO monitor{sizeof(monitor)};
    GetMonitorInfoW(MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST), &monitor);
    const RECT& area = monitor.rcWork;
    int next_x = rect.left + g_move_x;
    int next_y = rect.top + g_move_y;
    const int width = rect.right - rect.left;
    const int height = rect.bottom - rect.top;

    if (next_x < area.left || next_x + width > area.right) {
        g_move_x = -g_move_x;
        next_x = rect.left + g_move_x;
    }
    if (next_y < area.top || next_y + height > area.bottom) {
        g_move_y = -g_move_y;
        next_y = rect.top + g_move_y;
    }
    SetWindowPos(window, HWND_TOPMOST, next_x, next_y, 0, 0,
                 SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
}
}

LRESULT CALLBACK window_proc(HWND window, UINT message, WPARAM w_param, LPARAM l_param) {
    switch (message) {
    case WM_PAINT: {
        PAINTSTRUCT paint{};
        HDC dc = BeginPaint(window, &paint);
        draw_pet(window, dc);
        EndPaint(window, &paint);
        return 0;
    }
    case WM_TIMER:
        if (w_param == kAnimationTimer && g_image && !g_dimension.empty()) {
            g_frame = (g_frame + 1) % g_frame_count;
            g_image->SelectActiveFrame(reinterpret_cast<GUID*>(g_dimension.data()), g_frame);
            InvalidateRect(window, nullptr, FALSE);
            SetTimer(window, kAnimationTimer, frame_delay(g_frame), nullptr);
        }
        if (w_param == kMovementTimer) move_pet_slowly(window);
        return 0;
    case WM_LBUTTONDOWN:
        g_dragging = true;
        SetCapture(window);
        GetCursorPos(&g_drag_origin);
        GetWindowRect(window, &g_window_rect);
        return 0;
    case WM_MOUSEMOVE:
        if (g_dragging) {
            POINT point{};
            GetCursorPos(&point);
            SetWindowPos(window, HWND_TOPMOST,
                         g_window_rect.left + point.x - g_drag_origin.x,
                         g_window_rect.top + point.y - g_drag_origin.y,
                         0, 0, SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
        }
        return 0;
    case WM_LBUTTONUP:
        g_dragging = false;
        ReleaseCapture();
        return 0;
    case WM_RBUTTONUP: {
        POINT point{};
        GetCursorPos(&point);
        show_menu(window, point);
        return 0;
    }
    case WM_DESTROY:
        KillTimer(window, kAnimationTimer);
        KillTimer(window, kMovementTimer);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(window, message, w_param, l_param);
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    GdiplusStartupInput startup_input;
    if (GdiplusStartup(&g_gdiplus, &startup_input, nullptr) != Ok || !load_embedded_gif()) {
        MessageBoxW(nullptr, L"无法加载纸飞机动画。", L"Angelina 桌宠", MB_ICONERROR);
        return 1;
    }

    WNDCLASSW klass{};
    klass.hInstance = instance;
    klass.lpszClassName = L"AngelinaGifPet";
    klass.lpfnWndProc = window_proc;
    klass.hCursor = LoadCursor(nullptr, IDC_ARROW);
    RegisterClassW(&klass);

    HWND window = CreateWindowExW(WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
                                  klass.lpszClassName, L"Angelina 桌宠", WS_POPUP,
                                  CW_USEDEFAULT, CW_USEDEFAULT, 512, 512,
                                  nullptr, nullptr, instance, nullptr);
    SetLayeredWindowAttributes(window, kTransparentKey, 0, LWA_COLORKEY);
    ShowWindow(window, SW_SHOWNOACTIVATE);
    SetTimer(window, kAnimationTimer, frame_delay(0), nullptr);
    SetTimer(window, kMovementTimer, 100, nullptr);

    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0)) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    delete g_image;
    if (g_image_stream) g_image_stream->Release();
    GdiplusShutdown(g_gdiplus);
    return 0;
}
