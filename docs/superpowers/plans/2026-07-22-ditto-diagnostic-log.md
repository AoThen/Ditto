# Ditto 断言诊断日志添加 实现计划

> **面向 AI 代理的工作者：** 步骤使用复选框（`- [ ]`）语法来跟踪进度。

**目标：** 在 `ExitInstance` 中添加 Log 标记，定位 mtex.cpp:90 断言触发位置

**架构：** 在 `src/CP_Main.cpp` 的 `ExitInstance()` 中，每个操作步骤前后添加带 PID 的 Log 标记，并在 `CWinApp::ExitInstance()` 外层加 SEH 保护

**技术栈：** C++ / MFC / Win32 API

---

### 任务 1：ExitInstance 添加诊断 Log

**文件：**
- 修改：`src/CP_Main.cpp:960-996`（ExitInstance 方法体）

- [ ] **步骤 1：在 ExitInstance 中添加步骤 Log**

在 `ExitInstance()` 中每个操作前后添加 Log 标记，使用 `StrF` 格式化输出步骤编号和 PID。

**改前内容**（第 960-996 行）：
```cpp
int CCP_MainApp::ExitInstance() 
{
	Log(_T("ExitInstance"));

	// Stop Cloud Sync before database closes
	m_CloudSyncManager.Stop();

	// Wait for OCR threads to finish before cleanup
	int ocrWait = 0;
	while (g_ocrThreadCount > 0 && ocrWait < 30000)
	{
		Sleep(50);
		ocrWait += 50;
	}
	if (g_ocrThreadCount > 0)
		Log(_T("ExitInstance: OCR threads did not finish in 30s, proceeding"));
	CleanupOCR();

	DeleteDittoTempFiles(FALSE);

	m_db.close();

	if(m_pUacPasteThread != NULL)
	{
		if(m_pUacPasteThread->ThreadWasStarted() == false)
		{
			m_pUacPasteThread->FireExit();
		}
		delete m_pUacPasteThread;
	}

	Gdiplus::GdiplusShutdown(m_gdiplusToken);

	if(m_hMutex)
		CloseHandle(m_hMutex);

	return CWinApp::ExitInstance();
}
```

**改后内容**：
```cpp
int CCP_MainApp::ExitInstance() 
{
	Log(StrF(_T("ExitInstance - PID: %d"), GetCurrentProcessId()));

	// Stop Cloud Sync before database closes
	Log(StrF(_T("ExitInstance - Step 1 - Before CloudSync Stop - PID: %d"), GetCurrentProcessId()));
	m_CloudSyncManager.Stop();
	Log(StrF(_T("ExitInstance - Step 2 - After CloudSync Stop - PID: %d"), GetCurrentProcessId()));

	// Wait for OCR threads to finish before cleanup
	Log(StrF(_T("ExitInstance - Step 3 - Before OCR Wait - PID: %d"), GetCurrentProcessId()));
	int ocrWait = 0;
	while (g_ocrThreadCount > 0 && ocrWait < 30000)
	{
		Sleep(50);
		ocrWait += 50;
	}
	if (g_ocrThreadCount > 0)
		Log(_T("ExitInstance: OCR threads did not finish in 30s, proceeding"));
	CleanupOCR();
	Log(StrF(_T("ExitInstance - Step 4 - After OCR Wait - PID: %d"), GetCurrentProcessId()));

	Log(StrF(_T("ExitInstance - Step 5 - Before DeleteTemp - PID: %d"), GetCurrentProcessId()));
	DeleteDittoTempFiles(FALSE);
	Log(StrF(_T("ExitInstance - Step 6 - After DeleteTemp - PID: %d"), GetCurrentProcessId()));

	Log(StrF(_T("ExitInstance - Step 7 - Before db.close - PID: %d"), GetCurrentProcessId()));
	m_db.close();
	Log(StrF(_T("ExitInstance - Step 8 - After db.close - PID: %d"), GetCurrentProcessId()));

	Log(StrF(_T("ExitInstance - Step 9 - Before UAC Cleanup - PID: %d"), GetCurrentProcessId()));
	if(m_pUacPasteThread != NULL)
	{
		if(m_pUacPasteThread->ThreadWasStarted() == false)
		{
			m_pUacPasteThread->FireExit();
		}
		delete m_pUacPasteThread;
	}
	Log(StrF(_T("ExitInstance - Step 10 - After UAC Cleanup - PID: %d"), GetCurrentProcessId()));

	Log(StrF(_T("ExitInstance - Step 11 - Before GdiplusShutdown - PID: %d"), GetCurrentProcessId()));
	Gdiplus::GdiplusShutdown(m_gdiplusToken);
	Log(StrF(_T("ExitInstance - Step 12 - After GdiplusShutdown - PID: %d"), GetCurrentProcessId()));

	if(m_hMutex)
		CloseHandle(m_hMutex);

	Log(StrF(_T("ExitInstance - Step 13 - Before CWinApp::ExitInstance - PID: %d"), GetCurrentProcessId()));
	__try
	{
		return CWinApp::ExitInstance();
	}
	__except(EXCEPTION_EXECUTE_HANDLER)
	{
		Log(StrF(_T("ExitInstance - Step 14 - EXCEPTION in CWinApp::ExitInstance - PID: %d"), GetCurrentProcessId()));
		return 0;
	}
}
```

- [ ] **步骤 2：验证修改**

验证：代码语法正确，缩进一致，无遗漏括号。

- [ ] **步骤 3：Commit**

```bash
git add src/CP_Main.cpp
git commit -m "chore: ExitInstance 添加诊断 Log 标记定位断言触发点

- 每个操作步骤前后添加带 PID 的 Log 标记
- CWinApp::ExitInstance 外层加 SEH 保护
- 区分第一/第二实例的 ExitInstance 执行路径"
```