// Copyright 2023 Howaajin All rights reserved.

#include "UnrealWindowShadow.h"
#if PLATFORM_WINDOWS && UE_EDITOR
// clang-format off
#include "Interfaces/IMainFrameModule.h"
#include "Windows/WindowsApplication.h"
#include "Windows/WindowsWindow.h"
#include "Windows/AllowWindowsPlatformTypes.h"
#include "Styling/StyleColors.h"
// clang-format on
#include <ShellApi.h>
#include <dwmapi.h>

/// Undocumented API
/// https://stackoverflow.com/questions/65548316/change-color-of-borderless-window-in-win32
typedef enum _WINDOWCOMPOSITIONATTRIB
{
    WCA_UNDEFINED = 0,
    WCA_NCRENDERING_ENABLED = 1,
    WCA_NCRENDERING_POLICY = 2,
    WCA_TRANSITIONS_FORCEDISABLED = 3,
    WCA_ALLOW_NCPAINT = 4,
    WCA_CAPTION_BUTTON_BOUNDS = 5,
    WCA_NONCLIENT_RTL_LAYOUT = 6,
    WCA_FORCE_ICONIC_REPRESENTATION = 7,
    WCA_EXTENDED_FRAME_BOUNDS = 8,
    WCA_HAS_ICONIC_BITMAP = 9,
    WCA_THEME_ATTRIBUTES = 10,
    WCA_NCRENDERING_EXILED = 11,
    WCA_NCADORNMENTINFO = 12,
    WCA_EXCLUDED_FROM_LIVEPREVIEW = 13,
    WCA_VIDEO_OVERLAY_ACTIVE = 14,
    WCA_FORCE_ACTIVEWINDOW_APPEARANCE = 15,
    WCA_DISALLOW_PEEK = 16,
    WCA_CLOAK = 17,
    WCA_CLOAKED = 18,
    WCA_ACCENT_POLICY = 19,
    WCA_FREEZE_REPRESENTATION = 20,
    WCA_EVER_UNCLOAKED = 21,
    WCA_VISUAL_OWNER = 22,
    WCA_HOLOGRAPHIC = 23,
    WCA_EXCLUDED_FROM_DDA = 24,
    WCA_PASSIVEUPDATEMODE = 25,
    WCA_USEDARKMODECOLORS = 26,
    WCA_LAST = 27
} WINDOWCOMPOSITIONATTRIB;

typedef struct _WINDOWCOMPOSITIONATTRIBDATA
{
    WINDOWCOMPOSITIONATTRIB Attrib;
    PVOID pvData;
    SIZE_T cbData;
} WINDOWCOMPOSITIONATTRIBDATA;

typedef enum _ACCENT_STATE
{
    ACCENT_DISABLED = 0,
    ACCENT_ENABLE_GRADIENT = 1,
    ACCENT_ENABLE_TRANSPARENTGRADIENT = 2,
    ACCENT_ENABLE_BLURBEHIND = 3,
    ACCENT_ENABLE_ACRYLICBLURBEHIND = 4,    // RS4 1803
    ACCENT_ENABLE_HOSTBACKDROP = 5,         // RS5 1809
    ACCENT_INVALID_STATE = 6
} ACCENT_STATE;

typedef struct _ACCENT_POLICY
{
    ACCENT_STATE AccentState;
    DWORD AccentFlags;
    DWORD GradientColor;
    DWORD AnimationId;
} ACCENT_POLICY;

typedef BOOL(WINAPI* PFN_SetWindowCompositionAttribute)(HWND, WINDOWCOMPOSITIONATTRIBDATA*);
static PFN_SetWindowCompositionAttribute SetWindowCompositionAttribute;

WndProcFunc FUnrealWindowShadowModule::OldWndProc = NULL;
HWND FUnrealWindowShadowModule::RootHandle = NULL;
HHOOK FUnrealWindowShadowModule::CallWndProcHook = NULL;
TSharedPtr<class GenericApplication> FUnrealWindowShadowModule::App = NULL;
TSet<HWND> FUnrealWindowShadowModule::ProcessQueue;
TWeakPtr<SWindow> FUnrealWindowShadowModule::RootWindow;
FDelegateHandle FUnrealWindowShadowModule::MainFrameLoadDelegate;

typedef LRESULT(CALLBACK* WndProcFunc)(HWND hwnd, uint32 msg, WPARAM wParam, LPARAM lParam);

static bool IsWindowAncestor(HWND Ancestor, HWND HWnd)
{
    HWND parentWnd = HWnd;

    while (parentWnd != NULL)
    {
        if (Ancestor == parentWnd)
        {
            return true;
        }
        parentWnd = GetParent(parentWnd);
    }
    return false;
}

LRESULT CALLBACK FUnrealWindowShadowModule::WindowHookProc(int nCode, WPARAM wParam, LPARAM lParam)
{
    if (nCode == HC_ACTION)
    {
        CWPRETSTRUCT* pMsg = (CWPRETSTRUCT*) lParam;
        if (pMsg->message == WM_CREATE)
        {
            FUnrealWindowShadowModule::ProcessQueue.Add(pMsg->hwnd);
        }
        else if (pMsg->message == WM_DESTROY)
        {
            FUnrealWindowShadowModule::ProcessQueue.Remove(pMsg->hwnd);
        }
        else if (pMsg->message == WM_SHOWWINDOW && pMsg->wParam == TRUE)
        {
            if (FUnrealWindowShadowModule::ProcessQueue.Contains(pMsg->hwnd))
            {
                // Maybe menu window
                auto VisibleMenuWindow = FSlateApplication::Get().GetVisibleMenuWindow();
                if (VisibleMenuWindow.IsValid())
                {
                    TArray<TSharedRef<SWindow>> ChildWindows;
                    FSlateApplication::Get().GetAllVisibleWindowsOrdered(ChildWindows);
                    bool Supported = ChildWindows.ContainsByPredicate([pMsg](TSharedRef<SWindow> Window)
                        {
                            return Window->GetNativeWindow()->GetOSWindowHandle() == pMsg->hwnd && Window->GetType() == EWindowType::Menu;
                        });
                    if (Supported)
                    {
                        FUnrealWindowShadowModule::EnableSystemShadowForWindow(pMsg->hwnd);
                    }
                }
                // Others window
                else
                {
                    TArray<TSharedRef<SWindow>> ChildWindows;
                    if (RootWindow.Pin().IsValid())
                    {
                        FSlateApplication::Get().GetAllVisibleWindowsOrdered(ChildWindows);
                        bool Supported = ChildWindows.ContainsByPredicate([pMsg](TSharedRef<SWindow> Window)
                            {
                                bool SameHandle = Window->GetNativeWindow()->GetOSWindowHandle() == pMsg->hwnd;
                                bool SupportedType = Window->GetType() == EWindowType::Normal;
                                return SameHandle && SupportedType ;
                            });
                        if (Supported)
                        {
                            FUnrealWindowShadowModule::EnableSystemShadowForWindow(pMsg->hwnd);
                        }
                    }
                }
                FUnrealWindowShadowModule::ProcessQueue.Remove(pMsg->hwnd);
            }
        }
    }

    return CallNextHookEx(NULL, nCode, wParam, lParam);
}

static TSharedPtr<SWindow> FindWindowByHWND(const TArray<TSharedRef<SWindow>>& WindowsToSearch, HWND HandleToFind)
{
    for (int32 WindowIndex = 0; WindowIndex < WindowsToSearch.Num(); ++WindowIndex)
    {
        TSharedRef<SWindow> Window = WindowsToSearch[WindowIndex];
        if ((HWND) Window->GetNativeWindow()->GetOSWindowHandle() == HandleToFind)
        {
            return Window;
        }
    }

    return TSharedPtr<SWindow>(nullptr);
}

static RECT GetWorkAreaForWindow(HWND HWnd)
{
    HMONITOR monitor = MonitorFromWindow(HWnd, MONITOR_DEFAULTTONEAREST);
    if (!monitor)
    {
        return RECT{0};
    }

    MONITORINFO monitor_info = {0};
    monitor_info.cbSize = sizeof(monitor_info);
    if (!GetMonitorInfo(monitor, &monitor_info))
    {
        return RECT{0};
    }
    return monitor_info.rcWork;
}

LRESULT CALLBACK FUnrealWindowShadowModule::AppWndProc(HWND HWnd, uint32 msg, WPARAM wParam, LPARAM lParam)
{
    LRESULT Result = 0;
    switch (msg)
    {
        case WM_SIZE:
        {
            // Although it is possible to implement all the necessary operations to handle WM_SIZE and
            // avoid the performance degradation caused by SetWindowRgn,
            // different engine versions may have different implementations.
            // Therefore, the clipping area can only be removed after it has been set in the engine,
            // otherwise, the shadows outside the window cannot be displayed.
            Result = OldWndProc(HWnd, msg, wParam, lParam);
            ::SetWindowRgn(HWnd, NULL, false);
            return Result;
        }
        break;
        case WM_NCCALCSIZE:
        {
            if (!wParam)
            {
                // Needed or dwm will draw frame
                return 0;
            }
            else
            {
                return OldWndProc(HWnd, msg, wParam, lParam);
            }
        }
        break;
        case WM_GETMINMAXINFO:
        {
            WINDOWINFO WindowInfo;
            FMemory::Memzero(WindowInfo);
            WindowInfo.cbSize = sizeof(WindowInfo);
            ::GetWindowInfo(HWnd, &WindowInfo);

            MINMAXINFO* pMinMaxInfo = (MINMAXINFO*) lParam;
            RECT workArea = GetWorkAreaForWindow(HWnd);
            if ((workArea.bottom - workArea.top) == 0 && (workArea.right - workArea.left) == 0)
            {
                return OldWndProc(HWnd, msg, wParam, lParam);
            }
            pMinMaxInfo->ptMaxSize.y = workArea.bottom - workArea.top + WindowInfo.cyWindowBorders * 2;
            pMinMaxInfo->ptMaxSize.x = workArea.right - workArea.left + WindowInfo.cxWindowBorders * 2;
            return OldWndProc(HWnd, msg, wParam, lParam);
        }
        break;
        case WM_STYLECHANGED:
        {
            if (wParam == GWL_STYLE)
            {
                LPSTYLESTRUCT ps = (LPSTYLESTRUCT) lParam;
                if (ps->styleNew & WS_CAPTION)
                {
                    // Some where change the window style to has WS_CAPTION.
                    // With WS_CAPTION style will slow down the SetWindowRgn call.
                    uint32 WindowStyle = ps->styleNew;
                    WindowStyle &= (~WS_CAPTION);
                    ::SetWindowLong(HWnd, GWL_STYLE, WindowStyle);
                    return 0;
                }
            }
        }
        break;
        case WM_NCPAINT:
        case WM_NCACTIVATE:
            // Let Windows draw the shadow
            return DefWindowProc(HWnd, msg, wParam, lParam);
        default:
            break;
    }
    return OldWndProc(HWnd, msg, wParam, lParam);
}

void FUnrealWindowShadowModule::StartupModule()
{
    IMainFrameModule& MainFrameModule = FModuleManager::LoadModuleChecked<IMainFrameModule>(TEXT("MainFrame"));
    MainFrameLoadDelegate = MainFrameModule.OnMainFrameCreationFinished().AddRaw(this, &FUnrealWindowShadowModule::MainFrameLoad);
}

void FUnrealWindowShadowModule::ShutdownModule()
{
    if (::IsWindow(RootHandle))
    {
        ::SetWindowLongPtr(RootHandle, GWLP_WNDPROC, (LONG_PTR) OldWndProc);
        ::UnhookWindowsHookEx(CallWndProcHook);
    }
    IMainFrameModule& MainFrameModule = FModuleManager::LoadModuleChecked<IMainFrameModule>(TEXT("MainFrame"));
    MainFrameModule.OnMainFrameCreationFinished().Remove(MainFrameLoadDelegate);
}

static void SetDefaultBackgroundColor(HWND hwnd, FColor Color)
{
    ACCENT_POLICY accent = {
        ACCENT_ENABLE_GRADIENT,
        0,
        RGB(Color.R, Color.G, Color.B),
        0,
    };
    WINDOWCOMPOSITIONATTRIBDATA data;
    data.Attrib = WCA_ACCENT_POLICY;
    data.pvData = &accent;
    data.cbData = sizeof(accent);
    SetWindowCompositionAttribute(hwnd, &data);
}

void FUnrealWindowShadowModule::EnableSystemShadowForWindow(HWND HWnd)
{
    if (!::IsWindow(HWnd))
    {
        return;
    }
    if (!::SetWindowRgn(HWnd, NULL, false))
    {
        return;
    }
    const DWMNCRENDERINGPOLICY RenderingPolicy = DWMNCRP_ENABLED;
    HRESULT Result = ::DwmSetWindowAttribute(HWnd, DWMWA_NCRENDERING_POLICY, &RenderingPolicy, sizeof(RenderingPolicy));
    if (Result != S_OK)
    {
        return;
    }

    const BOOL bEnableNCPaint = true;
    verify(SUCCEEDED(::DwmSetWindowAttribute(HWnd, DWMWA_ALLOW_NCPAINT, &bEnableNCPaint, sizeof(bEnableNCPaint))));

    MARGINS Margins = {0, 0, 1, 0};
    verify(SUCCEEDED(::DwmExtendFrameIntoClientArea(HWnd, &Margins)));

    auto TitleColor = USlateThemeManager::Get().GetColor(EStyleColor::Title);
    SetDefaultBackgroundColor(HWnd, TitleColor.ToFColor(true));

    SetWindowLongPtr(HWnd, GWLP_WNDPROC, (LONG_PTR) FUnrealWindowShadowModule::AppWndProc);
    uint32 WindowStyle = ::GetWindowLong(HWnd, GWL_STYLE);
    WindowStyle &= (~WS_CAPTION);
    ::SetWindowLong(HWnd, GWL_STYLE, WindowStyle);
}

void FUnrealWindowShadowModule::MainFrameLoad(TSharedPtr<SWindow> InRootWindow, bool bIsRunningStartupDialog)
{
    bool IsEditorReopened = false;
    if (App.IsValid() && CallWndProcHook)
    {
        // Editor reopened
        IsEditorReopened = true;
        ::UnhookWindowsHookEx(CallWndProcHook);
        ProcessQueue.Empty();
    }
    App = FSlateApplication::Get().GetPlatformApplication();
    if (!App.IsValid())
    {
        return;
    }
    HMODULE user32dll = GetModuleHandleA("user32.dll");
    SetWindowCompositionAttribute = reinterpret_cast<PFN_SetWindowCompositionAttribute>(reinterpret_cast<void*>(GetProcAddress(user32dll, "SetWindowCompositionAttribute")));
    if (!SetWindowCompositionAttribute)
    {
        return;
    }
    RootHandle = (HWND) InRootWindow->GetNativeWindow()->GetOSWindowHandle();
    if (!::IsWindow(RootHandle))
    {
        return;
    }
    RootWindow = InRootWindow;
    if (!IsEditorReopened)
    {
        OldWndProc = (WndProcFunc) GetWindowLongPtr(RootHandle, GWLP_WNDPROC);
    }
    TArray<TSharedRef<SWindow>> Windows;
    FSlateApplication::Get().GetAllVisibleWindowsOrdered(Windows);
    for (auto Window : Windows)
    {
        HWND Handle = (HWND) Window->GetNativeWindow()->GetOSWindowHandle();
        EnableSystemShadowForWindow(Handle);
    }
    HINSTANCE hInstance = GetModuleHandle(NULL);
    CallWndProcHook = SetWindowsHookEx(WH_CALLWNDPROCRET, WindowHookProc, hInstance, GetWindowThreadProcessId(RootHandle, NULL));
}
#include "Windows/HideWindowsPlatformTypes.h"

#else

void FUnrealWindowShadowModule::StartupModule()
{
}
void FUnrealWindowShadowModule::ShutdownModule()
{
}

#endif

IMPLEMENT_MODULE(FUnrealWindowShadowModule, UnrealWindowShadowg)