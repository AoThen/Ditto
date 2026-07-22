# Ditto 单实例互斥量修复 实现计划

> **面向 AI 代理的工作者：** 必需子技能：使用 superpowers:subagent-driven-development（推荐）或 superpowers:executing-plans 逐任务实现此计划。步骤使用复选框（`- [ ]`）语法来跟踪进度。

**目标：** 修复 Ditto 关闭后立即重启时的 mtex.cpp:90 断言崩溃和幽灵进程残留问题

**架构：** 在 `CP_Main.cpp` 中做 3 处精准修改：(1) 第二实例检测到互斥量已存在时返回 `FALSE` 而非 `TRUE`；(2) `ExitInstance` 中关闭 `m_hMutex` 句柄；(3) 构造函数初始化 `m_hMutex` 为 `NULL`。同时将 `SendMessage` 改为 `SendMessageTimeout` 加 `IsWindow` 保护。

**技术栈：** C++ / MFC / Win32 API

---

### 任务 1：CP_Main.cpp 三处修复

**文件：**
- 修改：`src/CP_Main.cpp:136`（构造函数初始化）
- 修改：`src/CP_Main.cpp:386-397`（InitInstance 互斥量检测分支）
- 修改：`src/CP_Main.cpp:986`（ExitInstance 关闭句柄）

- [ ] **步骤 1：构造函数初始化 m_hMutex**

在 `src/CP_Main.cpp` 构造函数末尾（`m_dwRestartManagerSupportFlags` 初始化之后）添加 `m_hMutex = NULL;`。

**改前（第 187 行）：**
```cpp
m_dwRestartManagerSupportFlags = AFX_RESTART_MANAGER_SUPPORT_RESTART;
```

**改后：**
```cpp
m_dwRestartManagerSupportFlags = AFX_RESTART_MANAGER_SUPPORT_RESTART;

m_hMutex = NULL;
```

- [ ] **步骤 2：InitInstance 互斥量已存在分支返回 FALSE**

将 `SendMessage` 改为 `SendMessageTimeout`，加 `IsWindow` 保护，关闭句柄后返回 `FALSE`。

**改前（第 389-397 行）：**
```cpp
if(m_hMutex == NULL ||
    dwError == ERROR_ALREADY_EXISTS)
{
    Log(StrF(_T("Ditto is already running, closing, mutex: %s"), csMutex));
    HWND hWnd = (HWND)(LONG_PTR)CGetSetOptions::GetMainHWND();
    if(hWnd)
        ::SendMessage(hWnd, WM_SHOW_TRAY_ICON, TRUE, TRUE);

    return TRUE;
}
```

**改后：**
```cpp
if(m_hMutex == NULL ||
    dwError == ERROR_ALREADY_EXISTS)
{
    Log(StrF(_T("Ditto is already running, closing, mutex: %s"), csMutex));
    HWND hWnd = (HWND)(LONG_PTR)CGetSetOptions::GetMainHWND();
    if (hWnd && IsWindow(hWnd))
        ::SendMessageTimeout(hWnd, WM_SHOW_TRAY_ICON, TRUE, TRUE, SMTO_ABORTIFHUNG, 2000, NULL);

    if(m_hMutex)
        CloseHandle(m_hMutex);
    return FALSE;
}
```

- [ ] **步骤 3：ExitInstance 关闭互斥量句柄**

在 `ExitInstance` 中 `return CWinApp::ExitInstance();` 之前添加 `CloseHandle`。

**改前（第 984-987 行）：**
```cpp
Gdiplus::GdiplusShutdown(m_gdiplusToken);

return CWinApp::ExitInstance();
```

**改后：**
```cpp
Gdiplus::GdiplusShutdown(m_gdiplusToken);

if(m_hMutex)
    CloseHandle(m_hMutex);

return CWinApp::ExitInstance();
```

- [ ] **步骤 4：验证编译**

运行：
```bash
# 检查代码格式和语法正确性
# 这个项目是 C++/MFC Windows 项目，在当前 Linux 环境无法编译
# 代码审查验证即可
```

验证：确认修改后的代码语法正确、逻辑自洽。

- [ ] **步骤 5：Commit**

```bash
git add src/CP_Main.cpp
git commit -m "fix: 修复第二实例返回 TRUE 导致的幽灵进程和 mtex.cpp:90 断言

- 第二实例检测到互斥量已存在时返回 FALSE 而非 TRUE
- 将 SendMessage 改为 SendMessageTimeout + IsWindow 保护
- ExitInstance 中关闭 m_hMutex 句柄
- 构造函数初始化 m_hMutex = NULL"
```