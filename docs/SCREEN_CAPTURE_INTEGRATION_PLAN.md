# ScreenCapture DLL 插件集成方案

> 方案 C：进程内 DLL 集成
> 状态：待实施
> 功能分支：`feat/screen-capture-dll`

---

## 一、整体架构

```
Ditto.exe (MFC, C++17)
  │
  ├─ 托盘菜单 → ID_FIRST_SCREENCAPTURE (33004)
  │   └─ OnFirstScreencapture()
  │       → LoadLibrary("ScreenCaptureDll.dll")
  │       → StartScreenCapture(hWnd)
  │
  ├─ 全局热键 → Ctrl+Shift+Q（默认，可配置）
  │   └─ OnHotKey() → OnFirstScreencapture()
  │
  └─ 剪贴板监控（自动捕获截图结果）
      └─ 用户可直接从 Ditto 粘贴

ScreenCaptureDll.dll (Win32 DLL, C++20)
  │
  ├─ StartScreenCapture(HWND hParentWnd)   ← 唯一导出函数
  │   ├─ COM 初始化 (STA)
  │   ├─ 创建 App 单例（DLL 模块句柄）
  │   ├─ 设置 auto-quit=true
  │   ├─ D2D/D3D11/DWrite 设备创建（含 iconfont.ttf 内嵌资源）
  │   ├─ WinCap::init() → 全屏截图
  │   ├─ 嵌套消息循环 (GetMessage)
  │   │   ├─ 用户选区域
  │   │   ├─ WinPin::init() → 标注窗口
  │   │   ├─ 用户画标注 / Ctrl+C / 双击 / 工具栏按钮
  │   │   └─ WinPin::copyToClipboard()
  │   │       → Util::saveToClipboard()     ← 图像写入剪贴板
  │   │       → close() → onDestroy()
  │   │       → App::exit(0)               ← 清理 + PostQuitMessage(0)
  │   ├─ WM_QUIT 被消费，退出循环
  │   └─ 返回（清理已在 exit() 中完成）
  │
  └─ 结果：图像已在 Windows 剪贴板中
```

### 关键设计决策

1. **不通过 Ditto 插件接口**：现有插件系统只支持 `eFuncType_PRE_PASTE`（处理已有 clip），不适合"生成新内容"的截图场景
2. **嵌套消息泵**：DLL 内独立 `while(GetMessage())` 循环，`PostQuitMessage` 在此循环中被消费，不会传播到 Ditto 主消息泵
3. **自动退出机制**：利用 ScreenCapture 已有的 `auto-quit=true` 标志，保存完成后 `App::exit(0)` → `PostQuitMessage(0)`
4. **默认全局热键**：`Ctrl+Shift+Q`（0x0651），不冲突常见快捷键，可在 Ditto 设置中修改

---

## 二、新建项目：Addins/ScreenCaptureDll/

**位置：** 与 `Addins/DittoUtil/` 同级

**项目类型：** Win32 DLL（非 MFC），平台工具集 v143，C++20 `/std:c++20`

### 2.1 新建文件

| 文件 | 说明 |
|---|---|
| `dllmain.cpp` | DLL 入口，保存全局 `g_hDllInst` 模块句柄 |
| `ScreenCaptureAPI.h` | 导出 API 声明 |
| `ScreenCaptureAPI.cpp` | `StartScreenCapture()` 实现 |
| `pch.h` / `pch.cpp` | 预编译头（**去掉 WinRT/JSON 依赖**） |
| `ScreenCaptureDll.vcxproj` | 项目文件（参考 DittoUtil.vcxproj） |
| `ScreenCaptureDll.vcxproj.filters` | 筛选器 |
| `Res/Resource.rc` | 内嵌 iconfont.ttf + 版本信息 |
| `Res/iconfont.ttf` | 图标字体资源（从 ScreenCapture 拷贝） |
| `Res/logo.ico` | 应用图标（从 ScreenCapture 拷贝） |

### 2.2 从 ScreenCapture 拷贝的文件

```
App.h, App.cpp                    # 添加 DLL 模式支持
Util.h, Util.cpp                  # ✓ 纯 Win32，无改动
Lang.h, Lang.cpp                  # ✓ 纯 C++ std::map，无改动
History.h, History.cpp            # ✓ 撤销/重做，无改动
Setting.h, Setting.cpp            # 重构：去掉 WinRT JSON → std::map / 直接返回系统语言
Win/
  WinBase.h, WinBase.cpp          # ✓ 窗口基类，无改动
  WinCap.h, WinCap.cpp            # ✓ 截图主窗口，无改动
  WinCutMask.h, WinCutMask.cpp    # ✓ 选区遮罩，无改动
  WinPix.h, WinPix.cpp            # ✓ 取色器，无改动
  WinPin.h, WinPin.cpp            # ✓ 标注窗口，无改动
Shape/                            # ✓ 全部标注形状，无改动
  ShapeBase.h/cpp, ShapeRect.h/cpp
  ShapeEllipse.h/cpp, ShapeArrow.h/cpp
  ShapeNumber.h/cpp, ShapeLine.h/cpp
  ShapeText.h/cpp, ShapeMosaic.h/cpp
  ShapeEraser.h/cpp, ShapeTextWin.h/cpp
Tool/                             # ✓ 工具栏，无改动
  ToolBase.h/cpp, ToolMain.h/cpp, ToolSub.h/cpp
  ToolSubColor.h/cpp, ToolSubSlider.h/cpp
```

所有拷贝文件 `#include "pch.h"` 使用新 DLL 项目的 pch.h。

### 2.3 排除的文件（DLL 模式不需要）

| 文件 | 原因 |
|---|---|
| `main.cpp` | EXE 入口，由 DLL 入口替代 |
| `WinLong.*` / `WinVideo.*` | 长截图/录屏功能 |
| `WinSetting.*` / `WinSettingAbout.*` / `WinSettingCommon.*` / `WinSettingShortcut.*` | 设置窗口（Ditto 侧热键配置替代） |
| `ToolLong.*` / `ToolVideo.*` | 长截图/录屏工具栏 |
| `Tray.*` | 托盘（Ditto 已有托盘） |
| `cgif/*` | GIF 编码 |

---

## 三、ScreenCapture 代码修改清单

### 3.1 `pch.h` 简化

移除 WinRT 依赖行：
```cpp
// 删除以下两行 + using namespace
#include <winrt/Windows.Data.Json.h>
#include <winrt/Windows.Foundation.Collections.h>
using namespace winrt::Windows::Data::Json;
```

DLL 项目的 pch.h 不含 WinRT 引用，所有依赖 WinRT 的代码不会链接进来。

### 3.2 `ScreenCaptureAPI.h`（新建）

```cpp
#pragma once
#include <windows.h>

extern "C" {
    // 启动截图。hParentWnd: Ditto 主窗口句柄（预留，当前未使用）
    // 返回值: TRUE=成功（图像已写入剪贴板），FALSE=失败/用户取消
    BOOL WINAPI StartScreenCapture(HWND hParentWnd);
}
```

导出方式：`.def` 文件 或 `#pragma comment(linker, "/export:StartScreenCapture=...")`。

### 3.3 `ScreenCaptureAPI.cpp`（新建）

```cpp
#include "pch.h"
#include "App.h"
#include "Setting.h"
#include "Lang.h"
#include "Win/WinCap.h"

extern HINSTANCE g_hDllInst;  // 从 dllmain.cpp 获取

BOOL WINAPI StartScreenCapture(HWND hParentWnd)
{
    // COM 初始化：Ditto (MFC) 已是 STA，S_FALSE 表示已初始化，不 CoUninitialize
    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    BOOL bNeedCoUninit = (hr == S_OK);

    // 1) 创建 App 单例（DLL 模块句柄）
    //    WinBase::createWindow / getWinClsName 依赖 App::get()->hInstance
    //    App::dllInit() 封装了 args + 单例创建 + Setting/Lang 初始化
    //    ⚠ 不能使用 App::init()，它含 Tray::init() 等 EXE 专用逻辑
    App::setDllMode(true);
    App::setDllModule(g_hDllInst);
    App::dllInit(g_hDllInst);   // 内部: App::initArgs() + new App(hInst) + Setting::init() + Lang::init()

    WinCap::init();        // 内部调 App::initDevice() → 含 FindResource 用 dllModule

    // 2) 嵌套消息循环 — WM_QUIT 在此被消费，不传播到 Ditto
    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    // 3) 清理：App::exit(0) 已在 WinPin::onDestroy 中调过
    //    DLL 模式下 App::exit 跳过 WinVideo::stopIfRecording()，但正常执行 dispose
    WinCap::release(false);    // winCap 已 null，无操作

    if (bNeedCoUninit) CoUninitialize();

    return TRUE;
}
```

> **注意**：`App::dllInit()` 内部封装了 `app` 单例创建（`app` 变量在 App.cpp 的匿名命名空间中，不可跨 TU 直接访问）。

### 3.4 `App.h` — 添加 DLL 模式 API

```cpp
// 在 App 类声明中新增
static void setDllMode(bool val);
static bool isDllMode();
static void setDllModule(HINSTANCE hMod);
static HINSTANCE getDllModule();
```

### 3.5 `App.cpp` — 关键修改点

| 位置 | 改动 | 原因 |
|---|---|---|---|
| 构造函数 | DLL 模式跳过 `SetCurrentProcessExplicitAppUserModelID` | 进程标识属 Ditto，不冲突 |
| `initDevice()` | `FindResource(NULL, ...)` → `FindResource(getDllModule(), ...)`；同理 `LoadResource()` 和 `SizeofResource()` | 内嵌 iconfont.ttf 在 DLL 中，三处资源函数都需要 DLL 模块句柄 |
| `exit()` | DLL 模式：跳过 `WinVideo::stopIfRecording()`（不链接），但仍正常执行 `Setting::dispose()`、`Lang::dispose()`、`disposeDevice()` | 所有资源在 `exit()` 内释放完毕，消息循环后无需额外清理 |
| 全局变量 `app` | 保持在匿名命名空间，通过 `App::dllInit()` 公开方法访问 | 封装在 App 类内部，防止跨 TU 直接访问 |
| 新增 `dllInit()` | 封装 DLL 模式初始化：`App::initArgs()` + 设置 auto-quit/tray/enter 参数 + `new App(hInst)` + `Setting::init()` + `Lang::init()` | 清晰分离 EXE/DLL 入口 |
| `WinBase.cpp` line 157/162 | `LoadIcon(wcex.hInstance, (LPCTSTR)IDI_WINLOGO)` → `LoadIcon(NULL, IDI_WINLOGO)` | DLL 没有 IDI_WINLOGO 资源，改用系统图标 |

`App::exit()` 修改后：
```cpp
void App::exit(const int& code)
{
    if (!isDllMode()) {
        WinVideo::stopIfRecording();
    }
    // DLL 模式也正常 dispose（WinVideo 除外），所有资源在 exit() 内释放完毕
    Setting::dispose();
    Lang::dispose();
    disposeDevice();
    PostQuitMessage(code);
}
```

### 3.6 `Setting.h` / `Setting.cpp` — 去掉 WinRT JSON 依赖

**方案**：为 DLL 项目创建独立的 Setting 实现，不使用 WinRT：

```cpp
// Setting.h (DLL 版)
#pragma once
#include <string>

class Setting {
public:
    static void init();
    static void dispose();
    static std::wstring getLanguage();
};
```

```cpp
// Setting.cpp (DLL 版) — 无 WinRT，无 JSON，无文件读写
#include "pch.h"
#include "Setting.h"
#include "Util.h"

static bool g_init = false;

void Setting::init() {
    g_init = true;
}

void Setting::dispose() {
    g_init = false;
}

std::wstring Setting::getLanguage() {
    return Util::getSysLang();   // 直接获取系统语言
}
```

> **注意**：ScreenCapture 原生 Setting 完整的 `getConfigObj()`、`setKeys()`、`setSwitch()`、`save()` 等方法在 DLL 模式下不会被调用，不参与链接。

### 3.7 `Lang.cpp`

`Lang::init()` 调 `Setting::getLanguage()`，DLL 版直接返回系统语言。无需修改，链接 DLL 版 Setting 即可。

### 3.8 `dllmain.cpp`（新建）

```cpp
#include "pch.h"

HINSTANCE g_hDllInst = nullptr;

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved)
{
    switch (ul_reason_for_call)
    {
    case DLL_PROCESS_ATTACH:
        g_hDllInst = hModule;
        DisableThreadLibraryCalls(hModule);
        break;
    case DLL_THREAD_ATTACH:
    case DLL_THREAD_DETACH:
    case DLL_PROCESS_DETACH:
        break;
    }
    return TRUE;
}
```

---

## 四、Ditto 修改清单

### 4.1 `resource.h`

```cpp
#define ID_FIRST_SCREENCAPTURE      33004
// _APS_NEXT_COMMAND_VALUE 当前为 33004，使用后需更新为 33005
```

### 4.2 `CP_Main.rc` — 托盘菜单

在 `IDR_MENU` 中，`ID_FIRST_SAVECURRENTCLIPBOARD` 后，Exit 前的分隔符前添加：

```rc
MENUITEM SEPARATOR
MENUITEM "Screen Capture",          ID_FIRST_SCREENCAPTURE
MENUITEM SEPARATOR
```

最终结构：
```
Save Current Clipboard      ID_FIRST_SAVECURRENTCLIPBOARD
--- 分隔符 ---
Screen Capture              ID_FIRST_SCREENCAPTURE     ← 新增
--- 分隔符 ---
Exit                        ID_FIRST_EXIT
```

### 4.3 `MainFrm.h`

```cpp
// 消息处理函数声明
afx_msg void OnFirstScreencapture();
```

### 4.4 `MainFrm.cpp`

**消息映射**：在 `BEGIN_MESSAGE_MAP` 块中添加：
```cpp
ON_COMMAND(ID_FIRST_SCREENCAPTURE, OnFirstScreencapture)
```

**实现**：
```cpp
void CMainFrame::OnFirstScreencapture()
{
    static bool s_bCapturing = false;
    if (s_bCapturing) return;  // 防重入
    s_bCapturing = true;

    CString dllPath = CGetSetOptions::GetPath(PATH_ADDINS)
                    + _T("ScreenCaptureDll.dll");

    HMODULE hDll = LoadLibrary(dllPath);
    if (!hDll) {
        AfxMessageBox(_T("无法加载 ScreenCaptureDll.dll"));
        s_bCapturing = false;
        return;
    }

    typedef BOOL (WINAPI *FnStartCapture)(HWND);
    auto fn = (FnStartCapture)GetProcAddress(hDll, "StartScreenCapture");
    if (!fn) {
        FreeLibrary(hDll);
        AfxMessageBox(_T("ScreenCaptureDll.dll 导出函数未找到"));
        s_bCapturing = false;
        return;
    }

    fn(m_hWnd);  // 执行截图，结果写入剪贴板
    FreeLibrary(hDll);
    s_bCapturing = false;
}
```

**`OnHotKey()` 添加分支**（在 `m_pCopyAndSaveClipboard` 分支之后、`else` 之前——末尾 else 只处理 `m_clipId > 0` 的 clip 热键，截图热键不能放在那里）：
```cpp
else if (theApp.m_pScreenCaptureHotKey
         && wParam == theApp.m_pScreenCaptureHotKey->m_Atom)
{
    Log(_T("Screen capture hot key"));
    OnFirstScreencapture();
}
```

### 4.5 `CP_Main.h`

在 `m_pCopyAndSaveClipboard` 之后添加成员：
```cpp
CHotKey* m_pScreenCaptureHotKey;  // 截图热键
```

### 4.6 `CP_Main.cpp` — `AfterMainCreate()` 中注册

```cpp
// 在 m_pCopyAndSaveClipboard 之后添加
m_pScreenCaptureHotKey = new CHotKey("ScreenCaptureHotKey", 0x0651, false);
// 0x0651 = LOBYTE(VK_Q=0x51) | HIBYTE(MOD_CONTROL|MOD_SHIFT=0x06)
```

**无需手动 delete**：`CHotKey` 构造函数自动加入 `g_HotKeys` 全局数组，`CHotKeys` 析构时统一释放。

### 4.7 `OptionsKeyBoard.cpp` — 热键配置 UI

**同时修改以下文件：**

| 文件 | 修改内容 |
|---|---|
| `resource.h` | 新增 `IDC_HOTKEY_SCREENCAPTURE=2178`, `IDC_CHECK_WIN_SCREENCAPTURE=2179`, `IDC_STATIC_SCREENCAPTURE=2180`；更新 `_APS_NEXT_CONTROL_VALUE` 为 2181 |
| `CP_Main.rc` `IDD_OPTIONS_KEYSTROKES` | 在 Y=106 添加新行（Y=91 的 CopyAndSave 与 Y=110 的组框之间） |
| `OptionsKeyBoard.h` | 添加 `CHotKeyCtrl m_screenCaptureHotKey;` 成员 |

**`CP_Main.rc` 对话框资源修改**（在 `IDD_OPTIONS_KEYSTROKES` 的 Y=91 行后插入）：
```rc
    LTEXT           "Screen Capture",IDC_STATIC_SCREENCAPTURE,12,106,71,14,SS_CENTERIMAGE
    CONTROL         "",IDC_HOTKEY_SCREENCAPTURE,"msctls_hotkey32",WS_BORDER | WS_TABSTOP,128,106,80,14
    CONTROL         "Win",IDC_CHECK_WIN_SCREENCAPTURE,"Button",BS_AUTOCHECKBOX | WS_TABSTOP,210,106,33,14
```

**`DoDataExchange` 添加 DDX：**
```cpp
DDX_Control(pDX, IDC_HOTKEY_SCREENCAPTURE, m_screenCaptureHotKey);
```

**`OnInitDialog()` 中：**
```cpp
theApp.m_pScreenCaptureHotKey->CopyToCtrl(m_screenCaptureHotKey, m_hWnd, IDC_CHECK_WIN_SCREENCAPTURE);
```

**`OnApply()` 中：**
```cpp
theApp.m_pScreenCaptureHotKey->CopyFromCtrl(m_screenCaptureHotKey, m_hWnd, IDC_CHECK_WIN_SCREENCAPTURE);
```

**`bUnregOnShowDitto=false`**：与 `m_pSaveClipboard`/`m_pCopyAndSaveClipboard` 一致，截图热键在 Ditto 窗口显示时保持注册。

### 4.8 多语言支持

**`MultiLanguage.cpp`**：无需修改代码，框架会自动加载 `Ditto_Tray_Icon_Menu` 节。

**语言 XML 文件更新**（`Ditto/Debug/Language/*.xml`，共 30 个文件）：在每个语言文件的 `Ditto_Tray_Icon_Menu` 节添加：

```xml
<Item English_Text="Screen Capture" ID="33004">屏幕截图</Item>
```

各语言翻译示例：

| 语言 | 文件 | 翻译 |
|---|---|---|
| 中文 | `Chinese Simplified.xml` | `屏幕截图` |
| 英文 | `English.xml` | `Screen Capture` |
| 德语 | `Deutsch.xml` | `Bildschirmaufnahme` |
| 法语 | `French.xml` | `Capture d'écran` |
| ... | ... | ... |

> **说明**：未提供翻译的语言会直接显示 `English_Text` 属性的值。第一期可只翻译英文和中文，其余语言留空即可。

### 4.9 `CP_Main_10.sln`

添加 ScreenCaptureDll 项目引用：

```sln
Project("{...}") = "ScreenCaptureDll", "Addins\ScreenCaptureDll\ScreenCaptureDll.vcxproj", "{GUID}"
EndProject
```

---

## 五、默认全局热键：Ctrl+Shift+Q

### 选择理由

| 候选热键 | 评估 |
|---|---|
| `PrtSc` | ✗ Windows 原生占用，Ditto 已有监听 |
| `Win+Shift+S` | ✗ Win10/11 内置截图工具 |
| `Ctrl+Alt+C` | ✗ 可能与剪贴板工具冲突 |
| `Ctrl+Shift+Q` | ✓ 极少被占用，Q=Quick Capture |
| `Ctrl+Alt+Z` | △ 可用，但联想度低 |

**编码值：** `0x0651`
- `LOBYTE = 0x51` = `VK_Q`
- `HIBYTE = 0x06` = `MOD_CONTROL | MOD_SHIFT`

编码规则验证（与 Ditto 现有 `CHotKey` 系统一致）：
```
CHotKey::Register() → ::RegisterHotKey(hWnd, atom, GetModifier(), LOBYTE(m_Key))
GetModifier(HIBYTE(dwHotKey))  →  HIBYTE(0x0651)=0x06=HOTKEYF_CONTROL|HOTKEYF_SHIFT
```

### 用户可修改

热键存储在 Ditto 注册表（`CHotKey::SaveKey()` 写注册表），用户可通过：
**Ditto → 选项 → Keyboard Shortcuts → Screen Capture** 热键控件修改。

---

## 六、工作流程验证

```
1. 用户点击托盘 "Screen Capture" 或按 Ctrl+Shift+Q
2. OnFirstScreencapture()
3. LoadLibrary("ScreenCaptureDll.dll")
4. StartScreenCapture(m_hWnd)
   ├─ CoInitializeEx(STA)               → S_OK 或 S_FALSE
   ├─ App::initArgs()                   → 设置 auto-quit=true, tray=false
   ├─ App::setDllMode(true)             → 标记 DLL 模式
   ├─ app = new App(g_hDllInst)         → 用 DLL 模块句柄
   ├─ Setting::init()                   → DLL 版：直接返回系统语言
   ├─ Lang::init()                      → 加载 UI 字典
   ├─ WinCap::init()
   │   ├─ App::initDevice()             → D2D/D3D/DWrite, FindResource 用 dllModule
   │   └─ 全屏截图 (BitBlt)
   ├─ 嵌套消息循环 (GetMessage)
   │   ├─ 用户鼠标选区域
   │   ├─ WinCap::onMouseUp()
   │   │   ├─ WinPin::init()            → 标注窗口（创窗口 + 截图 + 工具栏）
   │   │   └─ WinCap::release(false)    → 关闭截图窗口
   │   ├─ 用户画标注 / Ctrl+C / 双击 / 工具栏确认
   │   ├─ WinPin::copyToClipboard()
   │   │   ├─ getImagePixels()          → CPU 可读位图
   │   │   ├─ Util::saveToClipboard()   → WIC 编码 PNG + CF_DIBV5
   │   │   └─ close()
   │   │       └─ onDestroy()
   │   │           └─ App::exit(0)
   │   │               ├─ (DLL 模式跳过 dispose)
   │   │               └─ PostQuitMessage(0)
   │   └─ WM_QUIT → GetMessage 返回 0 → 循环退出
   ├─ WinCap::release(false)            → winCap 已 null，无操作
   ├─ app.reset()
   ├─ Setting::dispose()                → 重置标志
   ├─ Lang::dispose()                   → 重置字典
   └─ CoUninitialize()                  → （仅当 S_OK 时）
5. FreeLibrary(hDll)                    → 卸载 DLL
6. Ditto 剪贴板监控自动捕获剪贴板中的图像
7. 用户可直接从 Ditto 粘贴图像
```

### 取消流程（Esc/右键）

```
用户按 Esc / 右键退出
  → WinPin::onKeyDown(VK_ESCAPE)
      → close() → onDestroy()
        → winPins.erase() → winPins 为空
        → App::disposeDeviceIfIdle()
          → 所有 Win* 均为 null → disposeDevice()
        → 消息循环无窗口，GetMessage 返回 -1？
```

**需特别注意：** 按 Esc 时，WinPin::onDestroy 中 `auto-quit=true` 为真，会调用 `App::exit(0)` → `PostQuitMessage(0)`。这是正确的 — 用户取消也应退出消息循环。剪贴板中没有新图像，Ditto 不会有新条目。

---

## 七、验证计划

| # | 验证内容 | 步骤 | 通过标准 |
|---|---|---|---|
| 1 | 编译 | 编译 ScreenCaptureDll.dll + Ditto.exe | 无编译/链接错误 |
| 2 | DLL 加载 | 点击托盘截图菜单 | LoadLibrary 成功，截图全屏覆盖 |
| 3 | 截图流程 | 全屏 → 选区 → 标注 → Ctrl+C | 图像写入剪贴板 |
| 4 | 工具栏确认 | 全屏 → 选区 → 标注 → 工具栏确认按钮 | 图像写入剪贴板 |
| 5 | 热键触发 | 按 Ctrl+Shift+Q | 截图窗口弹出 |
| 6 | Ditto 捕获 | 截图完成后 | 图像出现在 Ditto 列表中 |
| 7 | 热键修改 | 在设置中改为 Ctrl+Alt+A | 新键生效，旧键失效 |
| 8 | 取消截图 | Esc 退出 | 不写入剪贴板，Ditto 无新条目 |
| 9 | 连续截图 | 连续截图 5 次 | 每次结果正确，无崩溃 |
| 10 | 热键冲突 | 热键被其他应用占用 | 启动时日志记录失败，菜单仍可用 |
| 11 | DLL 缺失 | 删除 DLL 后点击菜单 | 提示"无法加载"错误消息 |

---

## 八、风险与边界条件

| 风险 | 可能性 | 影响 | 对策 |
|---|---|---|---|---|
| COM 模式冲突 (MTA vs STA) | 中 | `CoInitializeEx` 返回 `RPC_E_CHANGED_MODE` | Ditto 是 MFC STA，DLL 也用 STA，正常情况下不会冲突；若冲突，跳过 COM 初始化 |
| `App::exit()` 被 WinPin 和 StartScreenCapture 双重调用 | 中 | 可能二次释放 | DLL 模式的 `App::exit()` 正常 dispose（幂等）；消息循环后不再重复 dispose |
| `WinBase::close()` 重复调用 `onDestroy()` | 低 | winPins 二次 erase | winProc 无 WM_DESTROY 处理器，onDestroy 仅由 close() 显式调用一次，不重复 |
| 内嵌字体资源找不到 (`FindResource`/`LoadResource`/`SizeofResource`) | 中 | 图标字体不加载，工具栏按钮无图标 | 三处都使用 DLL 模块句柄而非 NULL，已验证 |
| `Setting` 仍引用 WinRT | 高 | 编译失败 | DLL 项目使用独立的 Setting 实现，无 WinRT 依赖 |
| `CHotKey` 自动加入 `g_HotKeys` | 低 | 无需手动 delete | 与现有模式一致（m_pPasteBuffer1 等均不手动 delete） |
| Win8 以下无 `D2D1_DEBUG_LEVEL_INFORMATION` | 低 | 调试模式下编译警告 | Release 构建不受影响 |
| `app` 单例跨 TU 不可访问 | 高 | 编译失败 | 通过 `App::dllInit()` 公开方法封装，而非直接操作 `app` 变量 |
| `IDI_WINLOGO` 在 DLL 中找不到 | 中 | 窗口类无图标 | 改为 `LoadIcon(NULL, IDI_WINLOGO)` 使用系统图标 |
| 语言 XML 文件未更新 | 中 | 截图菜单项显示原始 ID 字符串而非翻译 | 30 个 XML 文件逐一手动更新；英文可留空用 English_Text fallback |

---

## 九、文件清单汇总

### 新建文件（ScreenCaptureDll 项目，17 个）

```
Addins/ScreenCaptureDll/
├── dllmain.cpp
├── ScreenCaptureAPI.h
├── ScreenCaptureAPI.cpp
├── pch.h
├── pch.cpp
├── ScreenCaptureDll.vcxproj
├── ScreenCaptureDll.vcxproj.filters
├── Res/
│   ├── Resource.rc
│   ├── iconfont.ttf
│   └── logo.ico
└── (从 ScreenCapture 拷贝以下，共 ~30 个)
    ├── App.h, App.cpp
    ├── Util.h, Util.cpp
    ├── Lang.h, Lang.cpp
    ├── History.h, History.cpp
    ├── Setting.h, Setting.cpp          # DLL 简化版
    ├── Win/* (6 个)
    ├── Shape/* (11 个)
    └── Tool/* (7 个)
```

### 修改文件（ScreenCapture 源码，3 个）

| 文件 | 改动 |
|---|---|
| `App.h` | + 4 个静态方法声明：`setDllMode`, `isDllMode`, `setDllModule`, `getDllModule`；+ `dllInit()` 声明 |
| `App.cpp` | + 实现上述方法；`exit()` 中 DLL 模式跳过 `WinVideo::stopIfRecording()` 但正常 dispose 其他资源；`initDevice()` 中 `FindResource`/`LoadResource`/`SizeofResource` 用 `getDllModule()`；构造函数 DLL 模式跳过 `AppUserModelID` |
| `WinBase.cpp` | 两处 `LoadIcon(wcex.hInstance, IDI_WINLOGO)` 改为 `LoadIcon(NULL, IDI_WINLOGO)` |

### 修改文件（Ditto 侧，9 个 + 语言文件）

| 文件 | 改动 |
|---|---|
| `resource.h` | + `ID_FIRST_SCREENCAPTURE` (33004)；+ `IDC_HOTKEY_SCREENCAPTURE` (2178)、`IDC_CHECK_WIN_SCREENCAPTURE` (2179)、`IDC_STATIC_SCREENCAPTURE` (2180)；更新 `_APS_NEXT_COMMAND_VALUE` 为 33005、`_APS_NEXT_CONTROL_VALUE` 为 2181 |
| `CP_Main.rc` | IDR_MENU 添加 `MENUITEM "Screen Capture"`；IDD_OPTIONS_KEYSTROKES 添加 CHotKeyCtrl 控件 |
| `MainFrm.h` | + `afx_msg void OnFirstScreencapture()` |
| `MainFrm.cpp` | + 消息映射 + 实现 + OnHotKey 分支 |
| `CP_Main.h` | + `CHotKey* m_pScreenCaptureHotKey` |
| `CP_Main.cpp` | `AfterMainCreate()` 中 `new CHotKey("ScreenCaptureHotKey", 0x0651)` |
| `OptionsKeyBoard.h` | + `CHotKeyCtrl m_screenCaptureHotKey` 成员 |
| `OptionsKeyBoard.cpp` | DDX 绑定 + OnInitDialog 加载 + OnApply 保存 |
| `CP_Main_10.sln` | + ScreenCaptureDll 项目引用 |
| `Language/*.xml` | 各语言文件 `Ditto_Tray_Icon_Menu` 节添加 33004 条目（位于 `Debug/Language/` 目录下） |

---

## 十、工作量评估

| 项 | 预计 |
|---|---|
| ScreenCaptureDll 项目搭建（vcxproj + 文件整理 + 编译测试） | 1 天 |
| App.cpp 适配（DLL 模式 + FindResource） + Setting 简化版 | 0.5 天 |
| Ditto 菜单 + 热键 + OptionsKeyBoard UI | 0.5 天 |
| 多语言 XML 文件更新 | 0.5 天 |
| 联调测试（功能 + 边界 + 连续截图 + 热键修改） | 1 天 |
| **合计** | **3.5 天** |