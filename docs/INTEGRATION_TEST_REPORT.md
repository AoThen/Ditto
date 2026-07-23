# Ditto Cloud 三端联调测试报告

## 测试环境

| 组件 | 版本/配置 |
|------|----------|
| Go 后端 | Gin + GORM + SQLite, 端口 8080 |
| Vue 前端 | Vue 3 + Element Plus + Vite, 端口 5173 |
| 测试脚本 | Bash + curl + Python3 JSON 解析 |
| 测试时间 | 2026-04-07 |

## 测试结果

### E2E Integration Tests (13/13 ✅)

| # | 测试场景 | 状态 | 说明 |
|---|---------|------|------|
| 1 | 服务器健康检查 | ✅ | 返回 status=ok + 统计信息 |
| 2 | 用户注册 | ✅ | code=0, 返回 user_id |
| 3 | 用户登录 | ✅ | code=0, 返回 device_token + device_id |
| 4 | 推送剪贴板 | ✅ | 通过 /api/v1/clips/sync 推送成功 |
| 5 | 列表查询 | ✅ | 分页正确，返回 total 和 items |
| 6 | 搜索剪贴板 | ✅ | 按 description 模糊匹配 |
| 7 | 获取详情 | ✅ | 返回完整剪贴板数据（含 base64 格式） |
| 8 | 加密设置 | ✅ | POST /encryption/setup 返回 salt |
| 9 | 获取 salt | ✅ | GET /encryption/salt 返回相同 salt |
| 10 | 设备列表 | ✅ | 返回已注册设备列表 |
| 11 | 无效 Token | ✅ | 401 拒绝 |
| 12 | 限流测试 | ✅ | 连续 6 次失败登录触发 429 |
| 13 | 前端加载 | ✅ | Vue 前端正常渲染 HTML |

### Go Unit/E2E Tests (29/29 ✅)

| 模块 | 测试数 | 状态 |
|------|--------|------|
| Authentication | 9 | ✅ |
| Clips | 6 | ✅ |
| Sync | 3 | ✅ |
| Encryption | 3 | ✅ |
| Devices | 2 | ✅ |
| Health | 3 | ✅ |
| WebSocket | 5 | ✅ |

## 完整测试流程

### 1. 注册
```bash
POST /api/v1/auth/register
{"username":"testuser","email":"test@example.com","password":"testpass123"}
→ {"code":0,"message":"注册成功","data":{"user_id":1}}
```

### 2. 登录
```bash
POST /api/v1/auth/login
{"username":"testuser","password":"testpass123"}
→ {"code":0,"data":{"device_token":"eyJ...","device_id":"dev-1-"}}
```

### 3. 推送剪贴板
```bash
POST /api/v1/clips/sync
Authorization: Bearer eyJ...
{"since":"2000-01-01T00:00:00Z","device_id":"dev-1-","push_clips":[...]}
→ {"code":0,"data":{"updated_count":1,"sync_time":"..."}}
```

### 4. 查询列表
```bash
GET /api/v1/clips?page=1&per_page=10
Authorization: Bearer eyJ...
→ {"code":0,"data":{"items":[{"id":"clip-test-001","description":"Hello from Ditto Cloud!"}],"total":1}}
```

### 5. 搜索
```bash
GET /api/v1/clips?search=E2E
→ 正确过滤返回匹配结果
```

### 6. 获取详情
```bash
GET /api/v1/clips/clip-test-001
→ 返回完整数据，含 formats（base64 编码）
```

### 7. 加密设置
```bash
POST /api/v1/encryption/setup
→ {"code":0,"data":{"salt":"base64_32byte_salt","encryption_enabled":true}}
```

### 8. 获取 Salt
```bash
GET /api/v1/encryption/salt
→ {"code":0,"data":{"salt":"相同salt","encryption_enabled":true}}
```

### 9. 无效 Token 拒绝
```bash
GET /api/v1/clips
Authorization: Bearer invalid_token
→ HTTP 401
```

### 10. 限流触发
```bash
连续 6 次 POST /api/v1/auth/login（错误密码，相同 IP）
→ HTTP 429 "尝试次数过多，请 15 分钟后重试"
```

## 已验证的功能链路

```
✅ 注册 → 登录 → JWT 认证 → 推送剪贴板 → 查询列表 → 搜索 → 获取详情
✅ 加密 salt 生成 → 存储 → 重复查询
✅ 设备注册 → 设备列表
✅ 暴力破解防护（IP 限流 + 用户限流 + 持久化）
✅ JWT 过期检测
✅ WebSocket 实时推送（Unit 测试验证）
✅ 前端 WebSocket 集成（自动重连 + 状态指示器）
✅ C++ AES-256-GCM 加密模块（代码完成，待 Windows 编译）
```

## 运行方式

### 后端 E2E 测试
```bash
cd /home/git/working/Ditto/server
BCRYPT_COST=4 go test ./tests/... -v -count=1
```

### 集成测试（需要运行中的服务器）
```bash
# 启动后端
cd /home/git/working/Ditto/server && go run ./cmd/server/ &

# 运行集成测试
/home/git/working/Ditto/server/tests/integration_test.sh
```

### 前端可视化测试
```bash
# 启动前端
cd /home/git/working/Ditto/web && npm run dev

# 浏览器访问 http://localhost:5173
# 1. 注册账号
# 2. 登录
# 3. 进入 Dashboard → 剪贴板列表
# 4. 查看连接状态指示器（● 实时同步中）
# 5. 使用 curl 推送剪贴板 → 列表自动刷新
```

## 已知问题

| 问题 | 状态 |
|------|------|
| 限流中间件死锁 | ✅ 已修复 |
| bcrypt 性能（测试环境） | ✅ 通过 BCRYPT_COST=4 解决 |
| C++ Windows 编译 | ⏳ 待验证（代码完成，需 Windows 环境） |
