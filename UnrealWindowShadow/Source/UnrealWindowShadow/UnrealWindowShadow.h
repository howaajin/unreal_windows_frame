// Copyright 2023 Howaajin All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"
#include "Windows/WindowsHWrapper.h"

class FToolBarBuilder;
class FMenuBuilder;

typedef LRESULT(CALLBACK* WndProcFunc)(HWND hwnd, uint32 msg, WPARAM wParam, LPARAM lParam);

class FUnrealWindowShadowModule : public IModuleInterface
{
public:
    /** IModuleInterface implementation */
    virtual void StartupModule() override;
    virtual void ShutdownModule() override;
    static void EnableSystemShadowForWindow(HWND HWnd);
private:
    void MainFrameLoad(TSharedPtr<SWindow> InRootWindow, bool bIsRunningStartupDialog);

    static LRESULT CALLBACK AppWndProc(HWND hwnd, uint32 msg, WPARAM wParam, LPARAM lParam);
    static LRESULT CALLBACK WindowHookProc(int nCode, WPARAM wParam, LPARAM lParam);
    static WndProcFunc OldWndProc;
    static HWND RootHandle;
    static HHOOK CallWndProcHook;
    static TSharedPtr<class GenericApplication> App;
    static TSet<HWND> ProcessQueue;
    static TWeakPtr<SWindow> RootWindow;
    static FDelegateHandle MainFrameLoadDelegate;
};
