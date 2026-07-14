# Ditto 云端同步 - 端到端同步问题检查报告

> 检查日期：2026-04-12  
> 检查范围：C++ 客户端、Go 服务端、Web 前端全链路

---

## 一、已发现并修复的问题

### 问题 1: C++ 配置界面数据绑定不完整

**严重程度**: ⚠️ 中  
**影响范围**: 用户体验  
**状态**: ✅ 已修复

**问题描述**:
- `DoDataExchange` 缺少加密密码、加密状态、密钥文件路径的绑定
- 初始化时未加载完整的配置信息
- 状态显示为英文，不符合中文界面规范

**修复内容**:
```cpp
// 修复前：缺少字段绑定
DDX_Text(pDX, IDC_CLOUD_STATUS, m_csStatus);

// 修复后：完整绑定
DDX_Text(pDX, IDC_CLOUD_ENCRYPTION_PASSWORD, m_csEncryptionPassword);
DDX_Text(pDX, IDC_CLOUD_ENCRYPTION_STATUS, m_csEncryptionStatus);
DDX_Check(pDX, IDC_CLOUD_BTN_ENABLE_ENCRYPTION, m_bEncryptionEnabled);
DDX_Text(pDX, IDC_CLOUD_KEY_FILE_PATH, m_csKeyFilePath);
```

**验证方法**:
1. 编译 Ditto 项目
2. 打开"选项" → "云端同步"
3. 确认所有字段正确显示和保存

---

### 问题 2: 用户名和密钥文件路径未持久化

**严重程度**: ⚠️ 中  
**影响范围**: 用户体验  
**状态**: ✅ 已修复

**问题描述**:
- 登录成功后未保存用户名，下次打开需要重新输入
- 密钥文件导出后未保存路径，关闭对话框后丢失

**修复内容**:
```cpp
// Options.h 新增
static CString  GetCloudKeyFilePath();
static void     SetCloudKeyFilePath(LPCTSTR lpszValue);
static CString  GetCloudLastUsername();
static void     SetCloudLastUsername(LPCTSTR lpszValue);

// OptionCloud.cpp - 登录成功后保存
CGetSetOptions::SetCloudLastUsername(m_csUsername);

// 导出密钥后保存
CGetSetOptions::SetCloudKeyFilePath(filePath);
```

---

### 问题 3: 错误提示为英文

**严重程度**: ℹ️ 低  
**影响范围**: 中文用户体验  
**状态**: ✅ 已修复

**修复内容**:
- 所有 MessageBox 提示改为中文
- 状态信息改为中文
- 保留了异常信息的英文部分（技术错误码）

---

## 二、潜在的同步逻辑问题

### 问题 4: PullChanges 使用错误的 API 端点

**严重程度**: 🔴 高  
**影响范围**: 拉取同步失败  
**状态**: ⚠️ 需要修复

**问题描述**:
```cpp
// CloudSyncManager.cpp:PullChanges() - 第 846 行
auto res = cli.Post("/api/v1/clips/sync", bodyStr, "application/json");
```

C++ 客户端使用 `POST /clips/sync` 来拉取变更，但根据服务端设计：
- `POST /clips/sync` 主要用于推送（push_clips 字段）
- 拉取应该使用 `GET /clips/changes?since=xxx`

**当前行为**:
- 服务端 `Sync()` 方法同时处理 push 和 pull
- 当 `push_clips` 为空时，仍然会执行 pull 逻辑
- 虽然能工作，但不符合 RESTful 设计

**建议修复**:
```cpp
// 方案 1: 使用 GET /clips/changes（推荐）
auto res = cli.Get("/api/v1/clips/changes?since=" + sinceStr + 
                   "&device_id=" + m_deviceId);

// 方案 2: 保持现状，但在服务端增加日志说明
// 当前方案可接受，因为服务端确实处理了空 push 的 pull 请求
```

**优先级**: P1（当前可工作，但不符合最佳实践）

---

### 问题 5: PushNewClips 未处理 Token 过期

**严重程度**: 🟡 中  
**影响范围**: Token 过期后同步失败  
**状态**: ⚠️ 需要增强

**问题描述**:
```cpp
// PushNewClips() - 当返回 401 时
if (res->status == 200) {
    // 成功处理
} else {
    CString err;
    err.Format(_T("PushNewClips: server returned HTTP %d"), res->status);
    LogMessage(err);
    // 未处理 401，未触发重新认证
}
```

当 Token 过期（HTTP 401）时：
1. 只记录日志，不通知用户
2. 未触发重新登录流程
3. 后续同步会持续失败

**建议修复**:
```cpp
if (res->status == 401) {
    // Token 过期，清除登录状态
    CGetSetOptions::SetCloudDeviceToken("");
    
    // 通知用户（通过主窗口弹窗）
    ::PostMessage(AfxGetMainWnd()->GetSafeHwnd(), WM_CLOUD_AUTH_REQUIRED, 0, 0);
    
    LogMessage(_T("PushNewClips: Token expired, re-authentication required"));
    return;
}
```

**优先级**: P1（影响用户体验，但不影响核心功能）

---

### 问题 6: MergeRemoteClipToLocal 使用字符串拼接 SQL

**严重程度**: 🟡 中  
**影响范围**: SQL 注入风险  
**状态**: ⚠️ 需要修复

**问题描述**:
```cpp
// MergeRemoteClipToLocal() - 第 1065 行
csSQL.Format(_T("SELECT lID FROM Main WHERE CRC = %d OR mText = '%s' LIMIT 1"),
             crc, (LPCTSTR)escapedDesc);
```

虽然有 `escapedDesc` 转义单引号，但仍然：
1. 不够安全（可能有其他注入方式）
2. 不符合项目其他地方的参数化查询风格

**建议修复**:
```cpp
// 使用 CppSQLite3Statement
CppSQLite3Statement stmt = theApp.m_db.compileStatement(
    _T("SELECT lID FROM Main WHERE CRC = ? OR mText = ? LIMIT 1"));
stmt.bind(1, crc);
stmt.bind(2, desc);
CppSQLite3Query q = stmt.execQuery();
```

**优先级**: P1（当前有基本防护，但应改进）

---

### 问题 7: GetLocalClipsSince 使用 lDate 字段不正确

**严重程度**: 🔴 高  
**影响范围**: 增量同步可能遗漏剪贴板  
**状态**: ⚠️ 需要验证

**问题描述**:
```cpp
// GetLocalClipsSince() - 第 769 行
csSQL.Format(_T("SELECT lID, lDate, mText, CRC, ... FROM Main ")
             _T("WHERE lDate > %lld AND bIsGroup = 0 ..."), sinceTime);
```

**潜在问题**:
1. `lDate` 是剪贴板的**创建时间**，不是修改时间
2. 如果用户修改了剪贴板描述，`lDate` 不会更新
3. 可能导致修改后的剪贴板未同步

**需要验证**:
- Ditto 是否有 `lModifiedDate` 字段？
- 当前 `lDate` 是否在剪贴板修改时更新？

**建议**:
```sql
-- 如果存在修改时间字段
SELECT ... FROM Main WHERE lModifiedDate > %lld OR lDate > %lld

-- 如果只有 lDate，确保修改时也更新 lDate
UPDATE Main SET mText = ?, lDate = CURRENT_TIMESTAMP WHERE lID = ?
```

**优先级**: P0（可能导致数据不同步）

---

### 问题 8: 未处理服务端返回的 deleted_ids

**严重程度**: 🟡 中  
**影响范围**: 删除操作不同步  
**状态**: ⚠️ 需要实现

**问题描述**:
```cpp
// PullChanges() - 解析响应
if (!responseJson.contains("data") || !responseJson["data"].contains("new_clips"))
{
    LogMessage(_T("PullChanges: no new clips"));
    return;
}
```

当前只处理了 `new_clips`，未处理：
- `deleted_ids`: 其他设备删除的剪贴板 ID
- `updated_ids`: 其他设备修改的剪贴板 ID

**建议修复**:
```cpp
// 处理删除
if (responseJson["data"].contains("deleted_ids")) {
    for (const auto& deletedId : responseJson["data"]["deleted_ids"]) {
        DeleteLocalClip(deletedId.get<int>());
    }
}

// 处理更新（当前通过 new_clips 处理，因为 LWW 会覆盖）
```

**优先级**: P1（删除操作是核心功能）

---

## 三、性能和可靠性问题

### 问题 9: GetLocalClipsSince 限制 100 条可能导致遗漏

**严重程度**: 🟡 中  
**影响范围**: 大量剪贴板时同步不完整  
**状态**: ℹ️ 可接受，但需文档说明

**问题描述**:
```cpp
// GetLocalClipsSince() - 第 775 行
_T("ORDER BY lDate DESC LIMIT 100")
```

如果用户一次复制超过 100 条：
1. 只会推送最新的 100 条
2. 下次同步时会补齐剩余的（因为 `lastSyncTime` 未覆盖的部分）

**当前行为的合理性**:
- 避免单次推送数据过大
- 30 秒后会继续推送剩余的
- 符合"最终一致性"

**建议**:
- 文档中说明此限制
- 或者改为动态限制（根据数据大小调整）

**优先级**: P2（当前行为可接受）

---

### 问题 10: 加密初始化失败时未通知用户

**严重程度**: 🟡 中  
**影响范围**: 用户以为加密已启用，实际未生效  
**状态**: ⚠️ 需要增强

**问题描述**:
```cpp
// Initialize() - 第 91 行
if (!InitializeEncryption())
{
    OutputDebugString(_T("[CloudSync] WARNING: Encryption init failed, continuing without encryption.\n"));
    // 继续执行，不阻断
}
```

**风险**:
1. 用户以为数据已加密，实际是明文
2. 隐私泄露风险

**建议修复**:
```cpp
if (!InitializeEncryption())
{
    // 弹出警告
    MessageBox(nullptr, 
        _T("加密初始化失败！您的剪贴板数据将不会被加密。\n\n")
        _T("请在'云端同步'设置中重新启用加密。"),
        _T("云端同步警告"), MB_ICONWARNING);
    
    // 但仍然继续同步（明文）
    m_cryptoInitialized = FALSE;
}
```

**优先级**: P1（安全相关）

---

## 四、Web 前端问题

### 问题 11: WebSocket 断线后未请求缺失的消息

**严重程度**: ℹ️ 低  
**影响范围**: 断线期间可能遗漏推送  
**状态**: ℹ️ 当前设计可接受

**问题描述**:
- WebSocket 断线后，重连成功
- 但断线期间的推送消息可能丢失

**当前缓解措施**:
- 30 秒轮询兜底（`PullChanges`）
- 增量同步通过 `since` 参数保证完整性

**建议**:
- WebSocket 重连后，立即请求 `GET /clips/changes?since=last_ws_message_time`
- 补齐断线期间的消息

**优先级**: P2（有兜底机制）

---

## 五、服务端问题

### 问题 12: Sync 方法未验证 device_id 一致性

**严重程度**: ℹ️ 低  
**影响范围**: 审计准确性  
**状态**: ℹ️ 可接受

**问题描述**:
- 客户端可以在请求中伪造 `device_id`
- 当前使用 JWT 中的 `device_id`，但客户端请求体中的 `device_id` 未校验

**当前代码**:
```go
// clip_handler.go:Sync()
if req.DeviceID == "" {
    req.DeviceID = deviceID  // 从 JWT 提取
}
// 但如果 req.DeviceID 不为空，使用请求体中的值
```

**建议**:
```go
// 始终使用 JWT 中的 device_id，忽略请求体
req.DeviceID = deviceID  // 强制使用 JWT
```

**优先级**: P2（当前风险低，因为 JWT 不可伪造）

---

## 六、总结和建议

### 需要立即修复的问题（P0）

| 编号 | 问题 | 影响 | 建议 |
|------|------|------|------|
| 7 | GetLocalClipsSince 使用 lDate | 可能遗漏修改 | 验证字段语义，必要时修改 |

### 建议尽快修复的问题（P1）

| 编号 | 问题 | 影响 | 建议 |
|------|------|------|------|
| 4 | PullChanges 使用 POST 而非 GET | 不符合 RESTful | 改为 GET /clips/changes |
| 5 | 未处理 Token 过期 | 同步失败无提示 | 增加 401 处理和用户通知 |
| 6 | SQL 字符串拼接 | SQL 注入风险 | 改为参数化查询 |
| 8 | 未处理 deleted_ids | 删除不同步 | 增加删除处理逻辑 |
| 10 | 加密失败无提示 | 隐私泄露风险 | 增加弹窗警告 |

### 可接受但需文档说明的问题（P2）

| 编号 | 问题 | 影响 | 建议 |
|------|------|------|------|
| 9 | LIMIT 100 可能遗漏 | 延迟同步 | 文档说明 |
| 11 | WebSocket 断线遗漏 | 消息丢失 | 有轮询兜底 |
| 12 | device_id 校验不严 | 审计不准 | 强制使用 JWT |

---

## 七、修复计划

### 第一阶段：核心问题修复（1-2 天）

1. **验证 lDate 字段语义**
   - 检查 Ditto 数据库表结构
   - 确认修改时是否更新 lDate
   - 如有必要，改为使用修改时间

2. **实现 deleted_ids 处理**
   - 在 PullChanges 中增加删除处理
   - 测试删除同步

3. **SQL 参数化查询**
   - 修改 MergeRemoteClipToLocal
   - 使用 CppSQLite3Statement

### 第二阶段：增强健壮性（2-3 天）

4. **Token 过期处理**
   - 检测 401 响应
   - 清除 Token 并通知用户

5. **使用 GET /clips/changes**
   - 修改 PullChanges 使用正确的 API
   - 测试拉取功能

6. **加密失败提示**
   - 增加弹窗警告
   - 引导用户重新设置加密

### 第三阶段：优化（可选）

7. **WebSocket 断线补齐**
   - 重连后立即请求 changes
   - 完善实时同步

8. **强制使用 JWT device_id**
   - 服务端忽略请求体 device_id
   - 提高审计准确性

---

## 八、测试建议

### 端到端测试场景

1. **基本同步测试**
   - 设备 A 复制文本 → 设备 B 看到
   - 设备 B 复制图片 → 设备 A 看到
   - 设备 A 删除 → 设备 B 也删除

2. **加密测试**
   - 设备 A 启用加密 → 推送
   - 设备 B 导入密钥 → 拉取 → 解密成功
   - 验证服务端存储的是密文

3. **冲突测试**
   - 设备 A、B 同时复制不同内容
   - 验证 LWW 策略
   - 检查冲突副本

4. **网络异常测试**
   - 断网 → 复制 → 恢复网络 → 验证同步
   - Token 过期 → 验证重新认证

5. **文件同步测试**
   - 复制文件 → Web 端只看到路径
   - 验证文件内容未同步

---

*检查报告完毕*
