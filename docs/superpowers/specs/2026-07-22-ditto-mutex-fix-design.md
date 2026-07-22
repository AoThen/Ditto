# Ditto 单实例互斥量 Bug 修复设计

## 问题描述

关闭 Ditto 后立即重新打开，出现以下问题：
1. mtex.cpp:90 断言（CSingleLock::Lock 空指针）
2. 第二个进程无托盘图标但后台残留（幽灵进程）
3. 互斥量句柄泄漏

## 根本原因

### Bug 1：第二实例返回 `TRUE`（主线 Bug）

`CP_Main.cpp:396` 检测到 `CreateMutex` 返回 `ERROR_ALREADY_EXISTS` 时，执行 `return TRUE`。这告诉 MFC 初始化成功，进入消息泵循环。但由于 `CreateMainWnd()` 在此分支之上被跳过，进程 2 无主窗口/无托盘图标，成为幽灵进程。当进程 1 的 `ExitInstance` 清理全局资源时，进程 2 的空闲处理可能访问已被销毁的临界区，触发断言。

### Bug 2：第一实例退出窗口期过长

`ExitInstance` 中：
- `m_CloudSyncManager.Stop()` — 最长等待 30s
- OCR 线程等待循环 — 最长等待 30s
- 合计最长 60s 窗口期，期间互斥量被持有

### Bug 3：互斥量句柄泄漏

`m_hMutex` 在正常退出和 `ERROR_ALREADY_EXISTS` 分支均未调用 `CloseHandle`。

## 修复方案

### 修改 1：`CP_Main.cpp:396` — 发现已有实例时返回 FALSE

```cpp
// 改前：
return TRUE;

// 改后：
if (m_hMutex)
    CloseHandle(m_hMutex);
return FALSE;
```

同时将 `SendMessage` 改为 `SendMessageTimeout` 加 `IsWindow` 保护：

```cpp
// 改前：
HWND hWnd = (HWND)(LONG_PTR)CGetSetOptions::GetMainHWND();
if(hWnd)
    ::SendMessage(hWnd, WM_SHOW_TRAY_ICON, TRUE, TRUE);

// 改后：
HWND hWnd = (HWND)(LONG_PTR)CGetSetOptions::GetMainHWND();
if (hWnd && IsWindow(hWnd))
    ::SendMessageTimeout(hWnd, WM_SHOW_TRAY_ICON, TRUE, TRUE, SMTO_ABORTIFHUNG, 2000, NULL);
```

### 修改 2：`CP_Main.cpp:986` — ExitInstance 关闭互斥量句柄

```cpp
// 在 return CWinApp::ExitInstance(); 之前添加：
if (m_hMutex)
    CloseHandle(m_hMutex);
```

### 修改 3：`CP_Main.cpp:136` — 构造函数初始化 m_hMutex

```cpp
// 在构造函数末尾添加：
m_hMutex = NULL;
```

## 验证计划

1. 正常启动 Ditto — 启动成功，托盘图标正常
2. 打开多个 Ditto 实例 — 唯一实例运行，第二个正常退出
3. 关闭 Ditto → 瞬间重启 — 无断言，无幽灵进程残留
4. 完整功能回归 — 复制、历史记录、设置、CloudSync 正常
5. 检查泄漏 — 正常退出和互斥量检测到退出时，m_hMutex 均正确关闭

## 影响范围

- 仅修改 `src/CP_Main.cpp` 和 `src/CP_Main.h`
- 无测试文件需要修改（项目测试集中在 CloudSync 和 Server 模块，不涉及单实例初始化逻辑）