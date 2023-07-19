# Bring back the system shadow to the Unreal Editor in Windows

## Introduction

* Unreal has redrawn all the Windows's window elements, but they did not add window shadows.
When you want to use a flat theme, the editor window becomes difficult to distinguish the boundaries.

    ![comparison](images/comparison.png)

* This plugin has hacked their implementation to bring back the system window shadows while removing their rounded corners. Additionally, it speeds up the response time when opening windows.  
To clearly see the changes, I suggest using a flat editor theme. You can use my [Visual Studio Code theme](VisualStudioCode.json).  
Download and place it in the directory

    `%AppData%\..\Local\UnrealEngine\Slate\Themes`

    ![change theme](images/theme.png)

  Then launch Unreal Engine and select it in Editor Preferences->Appearance->Theme

  ![active theme](images/active_theme.png)
