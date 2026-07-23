# 修复退出时 Debug Assertion (mtex.cpp:90)

## 分析

调试断言 `mtex.cpp:90`（CSingleLock::Unlock → `ASSERT(m_pObject != NULL)`）在退出时发生，原因：

1. `InitInstance()` 之前跳过 `CWinApp::ExitInstance()`（避免第二实例因 AfxOleInit 未调用而断言），导致 MFC 内部状态（文档模板、OLE 等）未清理
2. 当 `AfxWinTerm()` 在模块卸载时尝试操作已部分销毁的 MFC 内部对象时，CSingleLock 的 `m_pObject` 为 NULL 触发断言

## 修复

在 `ExitInstance()` 中，保留所有 Ditto 自定义清理（OCR、DB、UAC 线程、GDI+、CloudSync、互斥体），之后**调用 `CWinApp::ExitInstance()` 替代 `return 0`**。

关键前提：`AfxOleInit()` 已移至互斥体检查之后，第二实例不会误调，因此 `CWinApp::ExitInstance()` 调用安全。

## 修改文件

`src/CP_Main.cpp` — `ExitInstance()` 函数，末尾改为：

```cpp
if(m_hMutex)
    CloseHandle(m_hMutex);

return CWinApp::ExitInstance();
```

## 验证

- 第一实例：按退出 → 不应再弹出 Debug Assertion 对话框
- 第二实例：启动后退出 → 不应弹出断言（`AfxOleInit` 未被调用，`CWinApp::ExitInstance` 安全）
- 进程退出后不应在后台残留