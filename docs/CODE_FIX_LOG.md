# CODE_FIX_LOG — 代码走查修复记录

日期：2026-08-08
范围：Go 云同步契约 + C++ 客户端安全/功能修复（A1-A10, B1-B7, C1-C5）
验证：Go 端 `go test ./...` 全绿（29 用例）；C++ 端经 GitHub Actions client-build-windows 验证

## 批 1：云同步契约（Go 服务端）

| 编号 | 文件 | 改动 |
|---|---|---|
| C1 | `server/internal/handler/clip_handler.go` | GET `/clips/changes` 响应体 `ChangesResponse` 增加 `dont_sync_ids` 字段（服务端 clip_service 本就填充，handler 出口补齐） |
| C2 | `server/internal/response/response.go` | `PaginatedResponse` 增加 `has_more` 字段；`group_service.go` ListGroups 计算 `page * perPage < total`（客户端 PullGroups 分页循环此前恒 false 提前终止） |
| C3 | `src/CloudSync/CloudSyncManager.{h,cpp}` | 新增 `CRITICAL_SECTION m_csGroupsPush` 串行化并发 PushGroups，杜绝并行线程重复建组 |
| C4 | `src/CloudSync/CloudSyncManager.cpp` | SyncThreadProc 中 PushNewClips 失败即跳过本轮 Pull，并置 Error 状态，避免凭据失效时每周期重复拉取/弹错窗 |
| C5 | `src/CloudSync/CloudSyncManager.cpp` | `EnsureHttpClient` 显式拒绝 `http://` 明文地址（与 CloudAuth 行为一致）；9 处请求调用点加 `m_httpClient ? ... : nullptr` 空保护 |

## 批 2：客户端内存/安全修复（P0）

| 编号 | 文件:行 | 改动 |
|---|---|---|
| A1 | `src/BitmapHelper.cpp` | 首循环 gdipBitmap 为 NULL 时跳过业务循环体，防空指针 |
| A2 | `src/EventThread.cpp` | 用加锁 `find()` + 判空替代裸 map 取值；缺失时重建 handle 向量并 continue |
| A3 | `src/QPasteWndThread.cpp` | `OnLoadAccelerators`/`OnUnloadAccelerators` 补 `m_csDb` 临界区 |
| A4 | `src/MainFrm.cpp` | `pTypes==NULL` 分支 `delete pClip; pClip=NULL` |
| A5 | `src/Misc.cpp` | `InternetEncode` 全部改 `delete[]`；首缓冲释放逻辑修正 |
| A6 | `src/Client.cpp` | `GlobalLock` 返回 NULL 时 Early-return FALSE |
| A7 | `src/CopyThread.cpp` | `IsClipboardViewerConnected`/`GetConnectCV`/`Quit` 对 `m_pClipboardViewer` 判空 |
| A8 | `src/QListCtrl.cpp`、`src/Md5.cpp` | 7 处 `delete` → `delete[]`（Tip 缓冲、MD5 读缓冲）；全库 new[]/delete[] 对称性复核通过 |
| A9 | `src/RecieveSocket.cpp` | 解密失败日志移除密码明文 |
| A10 | `src/QPasteWnd.cpp` | `OnDestroy` 先停 OCR 线程、再交`CWndEx::OnDestroy`，防悬挂读取 |

## 批 3：功能修正（B）

| 编号 | 文件 | 改动 |
|---|---|---|
| B1 | `src/CloudSync/CloudSyncManager.cpp` | 重写 `ExtractFilePathsFromHDROP`：支持 `encoding=base64` 解码 + DROPFILES 头解析（offset@0/fWide@16）+ UTF-16/ANSI 双 NUL 路径列表；越界防护完整 |
| B2 | `src/ImageViewer.{h,cpp}` | 恢复手势双指缩放（`ProcessZoom`：LockWindowUpdate→按比例缩放→滚动→刷新）；删除全部调试 `OutputDebugString` |
| B3 | `src/EditFrameWnd.{h,cpp}` | 移除 5 个 `ON_COMMAND(ID_BUTTON_*)` 死映射与 `OnDummy()` |
| B4 | `src/CP_Main.cpp`、`src/QPasteWnd.cpp` | `OnPasteCompleted` 保留为扩展点并注释；`OnMenuSenttoPromptforip` 标注未实现（改走 `PROMPT_SEND_TO_FRIEND`） |
| B5 | `src/Misc.cpp` | `IsVista()` 改用 `GetVersionExW`（major>=6），pragma 抑制 4996 告警 |
| B6 | `src/DatabaseUtilities.cpp` | `CompactDatabase`/`RepairDatabase` 由遗留 DAO 注释壳改为 SQLite：VACUUM 重建 + `PRAGMA integrity_check` 验证（独立连接 + 5s busy_timeout + 异常捕获） |
| B7 | `src/MainFrm.cpp` | `OnOcrCompleted` 全程持 `m_csDb` 锁保护读-改-写序列（保持 3s 退出等待不变） |

## 审计（阶段 3）发现并修复

| 严重级 | 问题 | 修复 |
|---|---|---|
| 必须 | PushGroups 锁泄漏：`CppSQLite3Exception` 不继承 `std::exception`，异常逃逸后 `m_csGroupsPush` 永不释放 → 永久死锁 | 补 `catch (...)` 兜底后统一 Leave |
| 建议 | SyncThreadProc 失败 continue 后状态残留 "Syncing..." | 置 Error + 错误信息 |
| 参考 | `OutputDebugStringA` 裸用、fNC 位未处理 | 影响可忽略，不修改 |

## 验证结果

- Go：`go test ./...`（含 tests/ 集成）全部通过；gofmt 干净
- C++：无法本机编译，依赖 CI `client-build-windows` job 验证（验证后补记结果）