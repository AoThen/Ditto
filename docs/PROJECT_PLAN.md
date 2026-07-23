# Ditto 云端同步 + Web 管理面板 - 项目计划书

> 基于 Ditto v3.25 代码库深度分析

---

## 一、项目背景

Ditto 是 Windows 平台经典的剪贴板管理器，核心特性：
- ✅ 本地 SQLite 存储所有剪贴板历史
- ✅ 支持文本、图片、HTML、文件等多种格式
- ✅ P2P 局域网同步（TCP Socket，端口 23443）
- ❌ **无云端同步、无 Web 管理、无多用户**

### 现有数据模型（4 张核心表）

| 表名 | 用途 | 关键字段 |
|------|------|----------|
| `Main` | 剪贴板元数据 | lID, lDate, mText, CRC, bIsGroup, lParentID, clipOrder |
| `Data` | 剪贴板数据 BLOB | lID, lParentID, strClipBoardFormat, ooData |
| `Types` | 类型定义 | lID, TypeText |
| `CopyBuffers` | 多复制缓冲区 | lID, lClipID, lCopyBuffer |

### 现有安全机制

- **加密算法**: SHA-256 密钥派生 + 10 万轮 AES-ECB 硬化 + AES-256-CBC 数据加密
- **默认密码**: `"LetMeIn"`（硬编码在 Options.cpp）
- **密码存储**: 明文在 HKCU 注册表或 Ditto.ini
- **网络认证**: 无握手，仅靠共享密码解密验证
- **多用户**: 不支持，纯单机

---

## 二、项目目标

1. **云端同步服务（Go）** - 多设备剪贴板实时同步
2. **Web 管理面板** - 浏览器查看、搜索、管理所有剪贴板
3. **Ditto 客户端集成** - 现有 C++ 程序接入云端同步
4. **安全优先** - 端到端加密、JWT 认证、HTTPS

### 核心数据模型：统一仓库

**一个用户 = 一个剪贴板仓库，所有设备共享同一份数据。**

```
用户 A 登录
├── 设备 PC-Office ──┐
├── 设备 PC-Home   ──┼──► 云端 clips (user_id = A) ──► 所有设备看到相同数据
├── 设备 Phone     ──┘
```

**关键行为**：
| 操作 | 效果 |
|------|------|
| 设备 A 复制新内容 | 自动同步到云端，设备 B/C 可见 |
| 设备 B 删除某条 | 全局删除，所有设备不可见（软删除） |
| 设备 C 修改描述 | 全局更新 |
| Web 端查看 | 看到所有设备推送的完整历史 |

`source_device_id` 字段仅用于**标记来源**和**审计追溯**，不做数据隔离。

---

## 三、认证方案设计

### 3.1 与 Ditto 现有认证的关系

| 维度 | Ditto 现有 | 云端方案 | 是否一致 |
|------|-----------|---------|---------|
| 用户体系 | 无 | JWT + 用户账号 | ❌ 新增 |
| 设备认证 | 无 | 设备 Token | ❌ 新增 |
| 数据加密 | 共享密码 AES-256 | 端到端加密（可选） | ✅ 理念一致 |
| 传输安全 | 裸 TCP + 应用层加密 | HTTPS/TLS | ✅ 更安全 |

### 3.2 为什么不能直接用 Ditto 的认证？

1. **Ditto 没有用户概念** - 没有登录/注册/账号体系
2. **默认密码 `"LetMeIn"` 是公开的** - 不能作为云端认证凭据
3. **密码明文存储** - 不符合云端安全标准
4. **无多租户** - 云端需要多用户隔离

### 3.3 云端认证设计

```
┌──────────────────────────────────────────────────────────────┐
│                     云端认证架构                               │
├──────────────────────────────────────────────────────────────┤
│                                                              │
│  用户注册/登录 ──► JWT Access Token (15min)                  │
│                   └── Refresh Token (7天, HTTP-only Cookie)  │
│                                                              │
│  设备注册 ──────► Device Token (绑定用户 + 设备指纹)          │
│                   └── Ditto 客户端使用此 Token 同步           │
│                                                              │
│  API 请求 ──────► Authorization: Bearer <JWT>                │
│  WebSocket ────► ?token=<JWT> (握手时验证)                    │
│                                                              │
└──────────────────────────────────────────────────────────────┘
```

#### Token 结构

```json
{
  "sub": "user_id",
  "device_id": "unique_device_fingerprint",
  "device_name": "DESKTOP-IT0LKM8",
  "iat": 1712476800,
  "exp": 1712477700,
  "type": "access"
}
```

#### 端到端加密（默认开启）

```
┌─────────────────────────────────────────────────────────────┐
│                    端到端加密流程                              │
│                                                             │
│  Salt 管理（服务端生成并存储）:                                │
│    首次设置                                                   │
│      用户设置密码 ──► POST /encryption/setup                │
│      服务端生成随机 salt ──► 存 user_settings 表             │
│      返回 salt ──► 客户端 PBKDF2(密码, salt) → AES 密钥      │
│                                                             │
│    后续设备登录                                               │
│      登录成功 ──► GET /encryption/salt ──► 下发同一 salt     │
│      用户输入密码 ──► PBKDF2(密码, salt) → 同一 AES 密钥     │
│                                                             │
│  推送剪贴板                                                   │
│    剪贴板数据 ──► AES-256-GCM 加密 ──► 密文 ──► HTTPS ──► 云 │
│                                                             │
│  拉取剪贴板                                                   │
│    云端密文 ──► HTTPS ──► AES-256-GCM 解密 ──► 明文展示      │
│                                                             │
│  云端视角                                                     │
│    存储的 data 字段 = AES-256-GCM 密文                        │
│    服务端无法解密，用户隐私得到保护                             │
│                                                             │
│  注意事项                                                     │
│    ⚠️ 忘记密码 = 数据不可恢复                                  │
│    ⚠️ 每个设备都需要输入密码（或导入密钥文件）                   │
└─────────────────────────────────────────────────────────────┘
```

> **设计原则**: 保持 Ditto 一贯的"低摩擦"体验——用户只需记住一个密码，其他全自动。

---

## 四、技术架构

### 4.1 整体架构

```
┌─────────────────┐         ┌──────────────────┐         ┌─────────────────┐
│  Ditto 客户端    │  HTTPS  │   Go 云端服务     │  HTTPS  │   Web 管理面板   │
│  C++ / Windows  │◄───────►│   (REST + WS)    │◄───────►│   Vue 3 SPA     │
│  (多设备)        │         │                  │         │   (浏览器)       │
└─────────────────┘         └──────────────────┘         └─────────────────┘
                                    │
                            ┌───────┼───────┐
                            │       │       │
                        PostgreSQL Redis  MinIO/S3
                        (主数据库)  (缓存)  (大文件)
```

### 4.2 技术栈选型

#### Go 后端

| 组件 | 技术 | 理由 |
|------|------|------|
| Web 框架 | **Gin** | 轻量、高性能、生态成熟 |
| ORM | **GORM** | 支持 PostgreSQL，自动迁移 |
| WebSocket | **gorilla/websocket** | 稳定、社区活跃 |
| 认证 | **golang-jwt/jwt** | JWT 标准实现 |
| 数据库 | **SQLite3** (个人) / **PostgreSQL 15+** (多用户) | 见下方 ADR-002 |
| 缓存 | ~~Redis~~ **Go 内存** | 去掉 Redis，单实例部署 |
| 对象存储 | ~~MinIO~~ **SQLite BLOB** | 去掉 MinIO，数据直接存 DB |
| 任务队列 | ~~Asynq~~ **goroutine** | 去掉 Redis，用 Go 原生协程 |
| 任务调度 | **cron** | 定时清理任务 |
| Salt 管理 | **服务端生成并存储** | 首次设置时生成，后续设备下发同一 salt |
| API 文档 | **swaggo/gin-swagger** | 自动生成 Swagger |
| 日志 | **zap** | 高性能结构化日志 |
| 配置 | **viper** | 多环境配置 |

#### Web 前端

| 组件 | 技术 | 理由 |
|------|------|------|
| 框架 | **Vue 3** | 响应式、生态丰富 |
| UI 库 | **Element Plus** | 企业级组件库 |
| 状态管理 | **Pinia** | Vue 3 官方推荐 |
| 路由 | **Vue Router 4** | 官方路由 |
| HTTP | **Axios** | 拦截器、自动 Token 刷新 |
| WebSocket | 原生 + 自动重连 | 轻量可控 |
| 构建工具 | **Vite 5** | 极速开发体验 |
| 语言 | **中文** | 界面语言 |
| HTTP 客户端 | **cpp-httplib** (`httplib.h`) | 单头文件，原生 HTTPS 支持 |
| JSON 库 | **nlohmann/json** (`json.hpp`) | 单头文件，C++11，API 友好 |
| 富文本 | **TinyMCE** | HTML 剪贴板预览 |
| 图片预览 | **Viewer.js** | 图片放大/旋转 |

---

## 五、数据库设计（SQLite3）

### 5.1 核心 ER 图

```
┌──────────┐ 1       N ┌──────────┐ 1       N ┌──────────────┐
│  users   │◄─────────►│  clips   │◄─────────►│ clip_formats │
└──────────┘           └──────────┘           └──────────────┘
      │ 1                    │
      │                      │ N
      │               ┌──────┴──────┐
      │               │  sync_logs  │
      │               └─────────────┘
      │
      │ 1                    N ┌──────────────┐
      │               ┌───────►│   devices    │
      │               │        └──────────────┘
      │ 1
      │               ┌──────────────┐
      └──────────────►│  user_settings │
                      └──────────────┘
```

> 技术栈：GORM 同时支持 SQLite 和 PostgreSQL，代码层无差异。
> 数据文件默认路径：`./data/ditto_cloud.db`

### 5.2 表结构详情

```sql
-- 注意：以下为 SQLite 语法，GORM 会自动适配
-- 数据文件路径: ./data/ditto_cloud.db

-- 用户表
CREATE TABLE users (
    id              INTEGER PRIMARY KEY AUTOINCREMENT,
    username        TEXT UNIQUE NOT NULL,
    email           TEXT UNIQUE NOT NULL,
    password_hash   TEXT NOT NULL,                -- bcrypt
    status          INTEGER DEFAULT 1,            -- 1=active, 0=disabled
    created_at      DATETIME DEFAULT CURRENT_TIMESTAMP,
    updated_at      DATETIME DEFAULT CURRENT_TIMESTAMP
);

-- 设备表
CREATE TABLE devices (
    id              INTEGER PRIMARY KEY AUTOINCREMENT,
    user_id         INTEGER NOT NULL REFERENCES users(id),
    device_id       TEXT NOT NULL,                -- 设备指纹
    device_name     TEXT,                         -- DESKTOP-XXX
    last_seen       DATETIME,
    created_at      DATETIME DEFAULT CURRENT_TIMESTAMP,
    UNIQUE(user_id, device_id)
);

-- 剪贴板主表（对齐 Ditto Main 表）
-- 设计原则：统一仓库，user_id 下所有设备共享数据
CREATE TABLE clips (
    id                  INTEGER PRIMARY KEY AUTOINCREMENT,
    user_id             INTEGER NOT NULL REFERENCES users(id),  -- 用户维度隔离
    remote_clip_id      TEXT,                     -- 客户端原始 ID
    description         TEXT,                     -- mText
    crc                 INTEGER,                  -- CRC32 去重校验
    is_group            INTEGER DEFAULT 0,        -- bIsGroup (SQLite 无 BOOLEAN)
    parent_id           INTEGER REFERENCES clips(id), -- lParentID
    clip_order          REAL,                     -- clipOrder
    sticky_order        REAL,                     -- stickyClipOrder
    shortcut            INTEGER,                  -- lShortCut
    global_shortcut     INTEGER,                  -- globalShortCut
    auto_delete         INTEGER DEFAULT 0,        -- lDontAutoDelete
    last_paste_date     DATETIME,
    source_device_id    INTEGER REFERENCES devices(id), -- 标记来源，不隔离数据
    encrypted           INTEGER DEFAULT 0,        -- 是否端到端加密
    created_at          DATETIME DEFAULT CURRENT_TIMESTAMP,
    updated_at          DATETIME DEFAULT CURRENT_TIMESTAMP,
    deleted_at          DATETIME                  -- 软删除
);

-- 索引
CREATE INDEX idx_clips_user_crc ON clips(user_id, crc);
CREATE INDEX idx_clips_user_updated ON clips(user_id, updated_at DESC);
CREATE INDEX idx_clips_user_group ON clips(user_id, is_group, parent_id);

-- 剪贴板格式表（对齐 Ditto Data 表）
CREATE TABLE clip_formats (
    id              INTEGER PRIMARY KEY AUTOINCREMENT,
    clip_id         INTEGER NOT NULL REFERENCES clips(id) ON DELETE CASCADE,
    format_name     TEXT NOT NULL,                -- CF_UNICODETEXT, CF_DIB, Rich Text Format
    data_type       TEXT DEFAULT 'inline',        -- inline | storage
    data            BLOB,                         -- 小数据直接存储 / 加密后 BLOB
    data_size       INTEGER NOT NULL,             -- 数据大小
    created_at      DATETIME DEFAULT CURRENT_TIMESTAMP
);

CREATE INDEX idx_formats_clip ON clip_formats(clip_id);
CREATE INDEX idx_formats_name ON clip_formats(format_name);

-- 同步日志
CREATE TABLE sync_logs (
    id              INTEGER PRIMARY KEY AUTOINCREMENT,
    user_id         INTEGER NOT NULL REFERENCES users(id),
    device_id       INTEGER REFERENCES devices(id),
    action          TEXT NOT NULL,                -- push | pull | delete
    clip_count      INTEGER DEFAULT 0,
    status          TEXT DEFAULT 'success',       -- success | failed | conflict
    error           TEXT,
    synced_at       DATETIME DEFAULT CURRENT_TIMESTAMP
);

CREATE INDEX idx_sync_logs_user_time ON sync_logs(user_id, synced_at DESC);

-- 用户设置（对齐 Ditto 注册表配置）
CREATE TABLE user_settings (
    user_id         INTEGER PRIMARY KEY REFERENCES users(id),
    max_clips       INTEGER DEFAULT 1000,
    max_storage_mb  INTEGER DEFAULT 100,
    auto_delete_days INTEGER DEFAULT 30,          -- 自动清理天数
    encryption_enabled INTEGER DEFAULT 1,         -- 端到端加密开关（默认开启）
    encryption_salt  BLOB,                        -- PBKDF2 salt（服务端生成，首次设置时写入）
    language        TEXT DEFAULT 'zh-CN',
    updated_at      DATETIME DEFAULT CURRENT_TIMESTAMP
);
```

---

## 六、API 设计

### 6.1 接口总览

```
Base URL: /api/v1

认证相关
  POST   /auth/register              # 用户注册
  POST   /auth/login                 # 登录
  POST   /auth/refresh               # 刷新 Token
  POST   /auth/device/register       # 设备注册
  POST   /auth/logout                # 登出

剪贴板
  GET    /clips                      # 列表（分页/搜索/过滤）
  GET    /clips/:id                  # 详情
  GET    /clips/:id/download         # 下载格式数据
  POST   /clips/sync                 # 批量推送
  GET    /clips/changes              # 增量同步 (?since=timestamp)
  DELETE /clips/:id                  # 删除
  POST   /clips/batch                # 批量操作
  DELETE /clips/clear                # 清空（可筛选）

分组
  GET    /groups                     # 分组列表
  POST   /groups                     # 创建分组
  PUT    /groups/:id                 # 更新分组
  DELETE /groups/:id                 # 删除分组

设备管理
  GET    /devices                    # 设备列表
  DELETE /devices/:id                # 移除设备

统计
  GET    /stats/overview             # 总览（剪贴板数、存储、趋势）
  GET    /stats/sync-logs            # 同步日志查询

WebSocket
  WS     /ws/sync                    # 实时同步推送
  WS     /ws/notifications           # 系统通知
```

### 6.2 核心接口示例

#### 增量同步（Pull）

```
GET /clips/changes?since=2024-04-07T10:30:00Z&device_id=xxx
Authorization: Bearer <JWT>

Response 200:
{
  "clips": [...],           // 自 since 以来变更的剪贴板
  "server_time": "2024-04-07T10:35:00Z",
  "has_more": false,
  "deleted_ids": [123, 456] // 已删除的 ID
}
```

#### 批量推送（Push）

```
POST /clips/sync
Authorization: Bearer <JWT>
X-Device-Id: xxx

{
  "clips": [
    {
      "remote_clip_id": "abc123",
      "description": "复制的代码片段",
      "crc": 1234567890,
      "formats": [
        {
          "format_name": "CF_UNICODETEXT",
          "data": "base64编码数据",
          "data_size": 1024
        }
      ]
    }
  ],
  "since": "2024-04-07T10:30:00Z"
}

Response 200:
{
  "synced_count": 1,
  "skipped_count": 0,    // 因 CRC 重复跳过
  "server_time": "2024-04-07T10:35:00Z"
}
```

#### 获取剪贴板列表

```
GET /clips?page=1&page_size=20&keyword=搜索&type=text&group_id=5
Authorization: Bearer <JWT>

Response 200:
{
  "total": 156,
  "clips": [
    {
      "id": 1,
      "description": "复制的代码片段",
      "formats": [
        {"format_name": "CF_UNICODETEXT", "data_size": 1024}
      ],
      "is_group": false,
      "created_at": "2024-04-07T10:00:00Z",
      "source_device": "DESKTOP-IT0LKM8"
    }
  ]
}
```

### 6.3 错误码规范

```json
{
  "code": 40001,
  "message": "Token 已过期",
  "details": null
}
```

| 错误码 | 含义 |
|--------|------|
| 40001 | Token 无效/过期 |
| 40002 | 设备未注册 |
| 40003 | 存储空间不足 |
| 40004 | 剪贴板数据过大 |
| 40005 | CRC 重复（客户端错误） |
| 50001 | 数据库错误 |
| 50002 | 对象存储错误 |

---

## 七、Go 项目结构

```
ditto-cloud-server/
├── cmd/
│   └── server/
│       └── main.go                 # 入口，初始化
├── internal/
│   ├── handler/                    # HTTP 处理器层
│   │   ├── auth_handler.go         # 认证接口
│   │   ├── clip_handler.go         # 剪贴板接口
│   │   ├── group_handler.go        # 分组接口
│   │   ├── device_handler.go       # 设备接口
│   │   └── stats_handler.go        # 统计接口
│   ├── service/                    # 业务逻辑层
│   │   ├── auth_service.go
│   │   ├── clip_service.go         # 核心同步逻辑
│   │   ├── sync_service.go         # 增量同步/冲突解决
│   │   └── storage_service.go      # 对象存储
│   ├── model/                      # 数据模型层
│   │   ├── user.go
│   │   ├── clip.go
│   │   ├── clip_format.go
│   │   ├── device.go
│   │   └── sync_log.go
│   ├── middleware/                  # 中间件
│   │   ├── auth.go                 # JWT 验证
│   │   ├── cors.go
│   │   ├── rate_limit.go           # 请求限流
│   │   └── logger.go               # 请求日志
│   ├── ws/                         # WebSocket
│   │   ├── hub.go                  # 连接管理
│   │   └── client.go               # 客户端连接
│   ├── crypto/                     # 加密工具
│   │   ├── aes.go                  # 端到端加密
│   │   └── crc32.go                # CRC 校验
│   └── repository/                 # 数据访问层
│       ├── clip_repo.go
│       └── user_repo.go
├── pkg/
│   ├── config/                     # 配置管理 (Viper)
│   ├── database/                   # 数据库初始化
│   └── response/                   # 统一响应格式
├── api/
│   └── swagger.yaml                # API 文档
├── configs/
│   ├── config.dev.yaml
│   ├── config.prod.yaml
│   └── config.default.yaml
├── migrations/
│   └── 001_init.sql                # 数据库初始迁移
├── docker-compose.yml              # 本地开发环境
├── Dockerfile
├── Makefile                        # 常用命令
├── go.mod
└── README.md
```

---

## 八、Web 前端项目结构

```
ditto-cloud-web/
├── src/
│   ├── api/                        # API 请求封装
│   │   ├── auth.js
│   │   ├── clips.js
│   │   ├── groups.js
│   │   └── stats.js
│   ├── components/                 # 公共组件
│   │   ├── ClipCard.vue            # 剪贴板卡片
│   │   ├── ClipDetailDialog.vue    # 详情弹窗
│   │   ├── FormatPreview.vue       # 格式预览
│   │   ├── GroupTree.vue           # 分组树
│   │   └── SyncStatus.vue          # 同步状态
│   ├── views/                      # 页面
│   │   ├── Login.vue               # 登录页
│   │   ├── Dashboard.vue           # 仪表盘
│   │   ├── Clips.vue               # 剪贴板管理
│   │   ├── Groups.vue              # 分组管理
│   │   ├── Devices.vue             # 设备管理
│   │   └── Settings.vue            # 设置
│   ├── stores/                     # Pinia 状态
│   │   ├── user.js
│   │   ├── clips.js
│   │   └── sync.js
│   ├── router/
│   │   └── index.js
│   ├── utils/
│   │   ├── request.js              # Axios 封装（Token 自动刷新）
│   │   ├── websocket.js            # WS 封装（自动重连）
│   │   └── format.js               # 格式转换工具
│   ├── composables/                # 组合式函数
│   │   ├── useClipSync.js
│   │   └── useClipboard.js
│   ├── styles/
│   │   └── variables.scss
│   ├── App.vue
│   └── main.js
├── public/
├── index.html
├── package.json
├── vite.config.js
└── README.md
```

---

## 九、Web 功能模块详述

### 9.1 仪表盘 (Dashboard)

```
┌─────────────────────────────────────────────────────┐
│  📊 剪贴板总览                                       │
│  ┌──────────┐ ┌──────────┐ ┌──────────┐ ┌─────────┐│
│  │ 总数      │ │ 今日新增  │ │ 存储使用  │ │ 设备数  ││
│  │  1,234   │ │    56    │ │ 45/100MB │ │    3    ││
│  └──────────┘ └──────────┘ └──────────┘ └─────────┘│
│                                                     │
│  📈 近 7 天趋势图 (折线图)                            │
│  📋 最近同步的剪贴板 (列表)                           │
│  🔔 同步异常告警                                      │
└─────────────────────────────────────────────────────┘
```

### 9.2 剪贴板管理 (Clips)

- **列表视图**: 分页表格，支持搜索、格式过滤、分组筛选
- **详情弹窗**: 多 Tab 预览（文本/图片/HTML/原始格式）
- **批量操作**: 多选删除、移动分组、置顶
- **实时同步**: WebSocket 推送新剪贴板，自动刷新列表
- **复制功能**: 一键复制到系统剪贴板（文本）、下载（图片/文件）

### 9.3 分组管理 (Groups)

- **树形结构**: 可视化分组层级
- **拖拽排序**: 调整剪贴板所属分组
- **快捷操作**: 创建/重命名/删除分组

### 9.4 设备管理 (Devices)

- **设备列表**: 名称、最后在线时间、状态
- **移除设备**: 踢出不可信设备
- **同步日志**: 查看每个设备的同步记录

### 9.5 设置 (Settings)

- **个人信息**: 头像、密码修改
- **存储设置**: 自动清理规则、存储配额
- **安全设置**: 端到端加密开关、Token 管理
- **语言切换**: 中/英文

---

## 十、Ditto 客户端集成方案

### 10.1 新增文件

```
src/
├── CloudSyncClient.h/cpp        # HTTP/WebSocket 客户端
├── CloudSyncManager.h/cpp       # 同步管理器
├── CloudAuth.h/cpp              # 云端认证
└── CloudLoginDialog.h/cpp       # 登录对话框
```

### 10.2 集成要点

```cpp
// CloudSyncManager 核心接口
class CCloudSyncManager {
public:
    BOOL   Initialize();                          // 初始化（读取配置、启动同步线程）
    BOOL   Login(const CString& server, const CString& token);
    void   PushNewClips(const CClipList& clips);  // 推送新剪贴板
    CClipList PullChanges(time_t since);           // 拉取变更
    void   OnClipAdded(CClip* pClip);              // 本地新增时触发同步
    void   StartBackgroundSync();                  // 后台定时同步
    
private:
    CString m_serverUrl;
    CString m_deviceToken;
    time_t  m_lastSyncTime;
    
    BOOL   SendHTTPRequest(const CString& method, const CString& path, 
                           const CString& body, CString& response);
    BOOL   ConnectWebSocket();                      // 实时同步
    void   HandleConflict(const CClip& remote);     // 冲突处理
};
```

### 10.3 文件类型剪贴板：不同步文件内容

#### Ditto 现有机制

Ditto 对文件复制（`CF_HDROP` 格式）采用**两阶段懒加载**：

```
复制文件 ─► 本地只存文件路径（不存文件内容）
    │
P2P 同步 ─► 只发送路径 + 来源IP/电脑名
    │
用户粘贴时 ─► 反向TCP连回源机器 ─► 拉取实际文件 ─► 存 %TEMP%
```

**Ditto 从未真正同步过文件内容。**

#### 云端方案

| 原因 | 说明 |
|------|------|
| 文件可能极大 | 几GB视频/压缩包，云端存不起 |
| 路径是机器相关的 | `C:\Users\Alice\report.pdf` 在B电脑不存在 |
| 与Ditto设计一致 | 文件内容本来就是按需拉取 |
| 隐私保护 | 文件可能含敏感数据 |

```
用户复制文件（CF_HDROP）
    │
    ▼
本地 SQLite ──► 保存路径（明文，与现有行为一致）
    │
    ▼
云端同步 ──► 只同步"复制了什么文件"的元信息
    │
    ▼
clip_formats 表:
    format_name = "CF_HDROP"
    data = AES-GCM 加密后的路径文字
    data_size = 路径文字大小（NOT 文件大小）
```

**效果**：
| 端 | 能看到什么 |
|----|-----------|
| Web 端 | "复制了文件 report.pdf"，但无法下载文件 |
| 设备 B | 能看到文件记录，但路径可能无效 |
| 本地 | 文件剪贴板正常使用，完全不受影响 |

### 10.4 集成点

| Ditto 现有代码 | 集成方式 |
|---------------|----------|
| `Clip.cpp` - `CClip::SaveToDatabase()` | 保存本地后，加入待同步队列 |
| `DatabaseUtilities.cpp` - 新增剪贴板 | 触发 `CloudSyncManager::OnClipAdded()` |
| `AutoSendToClientThread.cpp` | 参考其线程模型，实现云端后台同步线程 |
| `CP_Main.cpp` - 启动初始化 | 新增 `CloudSyncManager::Initialize()` |
| `Options.h` | 新增云端服务器地址、Token 等配置项 |
| `CF_HDropAggregator.cpp` | **跳过**文件内容同步，只同步路径 |

---

## 十一、安全设计

### 11.1 传输安全

```
所有通信 ──► HTTPS (TLS 1.2+)
WebSocket ─► WSS (TLS)
```

### 11.2 存储安全

```
┌─────────────────────────────────────────────┐
│              数据加密策略                      │
├─────────────────────────────────────────────┤
│                                             │
│  所有数据 ──► 端到端加密 ──► SQLite BLOB    │
│                                             │
│  加密算法: AES-256-GCM                      │
│  密钥派生: PBKDF2(用户密码, salt, 10万轮)     │
│                                             │
│  云端视角: 只存密文，无法解密                  │
│  用户视角: 输入密码后自动解密                  │
│                                             │
│  元数据（不解密）:                            │
│    - description（预览文字，非敏感）           │
│    - crc（去重用）                           │
│    - created_at, source_device 等            │
│                                             │
│  敏感数据（端到端加密）:                       │
│    - clip_formats.data（实际剪贴板内容）       │
│                                             │
└─────────────────────────────────────────────┘
```

### 11.3 暴力破解防护

```
┌─────────────────────────────────────────────────────────────┐
│                   登录暴力破解防护                             │
│                                                             │
│  POST /api/v1/auth/login                                    │
│                                                             │
│  内存限流器 + SQLite 持久化（重启不丢失）                      │
│  ┌───────────────────────────────────────────┐             │
│  │  维度      │ 阈值    │ 惩罚                │            │
│  ├───────────────────────────────────────────┤             │
│  │  单 IP     │ 5次/分钟 │ 封禁 15 分钟       │            │
│  │  单用户    │ 10次/小时│ 锁定 1 小时        │            │
│  └───────────────────────────────────────────┘             │
│                                                             │
│  攻击者尝试:                                                 │
│    1. 爆破 4 次 ──► 服务重启 ──► 计数仍在 SQLite 中         │
│    2. 第 5 次请求 ──► 429 "请 15 分钟后重试"                │
│    3. 换 IP 继续 ──► 用户名维度仍然计数 ──► 锁定 1 小时      │
│                                                             │
│  密码存储:                                                   │
│    bcrypt(cost=12) 哈希，服务端永不存明文                     │
│                                                             │
│  密码要求（注册时）:                                          │
│    • 最少 8 位                                               │
│    • 不强制复杂度（降低使用摩擦）                              │
│                                                             │
│  响应设计:                                                   │
│    • 成功: 200 + device_token                                │
│    • 失败: 401 + "用户名或密码错误"（不区分原因）              │
│    • 封禁: 429 + "尝试次数过多，请 15 分钟后重试"             │
│    • 锁定: 423 + "账号已锁定，请 1 小时后重试"                │
│                                                             │
│  限流数据持久化:                                              │
│    表: rate_limit_records                                   │
│    字段: key, fail_count, ban_until                         │
│    写频率: 仅登录失败时（正常用户无影响）                      │
│    重启: 从 SQLite 恢复到内存 map                            │
│                                                             │
│  手动重置限流（运维用）:                                       │
│    场景: 用户忘记密码被锁 / 误封正常 IP                        │
│                                                             │
│    sqlite3 ditto_cloud.db                                   │
│                                                             │
│    -- 查看所有封禁记录                                       │
│    SELECT * FROM rate_limit_records;                         │
│                                                             │
│    -- 重置指定用户                                           │
│    DELETE FROM rate_limit_records                            │
│    WHERE key = 'user:zhangsan';                              │
│                                                             │
│    -- 重置指定 IP                                            │
│    DELETE FROM rate_limit_records                            │
│    WHERE key = 'ip:192.168.1.100';                           │
│                                                             │
│    -- 清空所有限流记录（紧急情况）                             │
│    DELETE FROM rate_limit_records;                            │
│                                                             │
│    生效: 下次请求时 Go 进程会从 DB 重新加载，已删除的记录不再生效 │
│                                                              │
│    注意: 如果 Go 进程正在运行，内存中的计数不会立即清空         │
│          等待 ban_until 过期自动清理，或重启服务强制重新加载     │
│                                                             │
└─────────────────────────────────────────────────────────────┘
```

### 11.4 Token 安全

```
┌─────────────────────────────────────────────────────────────┐
│                    JWT Token 安全设计                         │
│                                                             │
│  Device Token（长期凭证）                                     │
│    • 有效期: 90 天                                           │
│    • 载荷: { user_id, device_id, device_name }              │
│    • 签名: HMAC-SHA256                                      │
│    • 客户端存储: HKCU\Software\Ditto\CloudDeviceToken       │
│    • 传输: Authorization: Bearer <token>                    │
│    • 撤销: 用户可在 Web 端踢掉指定设备                        │
│                                                             │
│  Token 泄露应对:                                              │
│    1. 用户发现异常 ──► Web 端"设备管理"                        │
│    2. 点击"移除设备" ──► Token 立即失效                       │
│    3. 下次请求 ──► 401 未授权                                │
│    4. 被踢设备 ──► 弹出登录框，需要重新认证                   │
│                                                             │
│  Token 自动刷新:                                              │
│    • 客户端维护 Token 有效期                                   │
│    • 过期前 7 天自动续期（静默）                               │
│    • 续期失败 ──► 弹出登录框                                   │
│                                                             │
└─────────────────────────────────────────────────────────────┘
```

### 11.5 端到端加密密码保护

```
┌─────────────────────────────────────────────────────────────┐
│                端到端加密密码（用户设置的云端密码）               │
│                                                             │
│  用途: AES-256-GCM 加解密剪贴板内容                           │
│  比登录密码更敏感，因为丢了数据就无法恢复                        │
│                                                             │
│  客户端保护:                                                   │
│    • 不存明文，只存 PBKDF2 派生的密钥                          │
│    • 密钥存注册表 HKCU，但不可逆                               │
│    • 每次使用直接从注册表读密钥，不需要用户重复输入              │
│                                                             │
│  忘记密码场景:                                                 │
│    • 新设备首次登录 ──► 必须输入端到端密码                      │
│    • 忘记密码 ──► 云端密文无法解密 ──► 数据丢失                 │
│    • 缓解方案: 支持导出密钥文件 (.dittokey)                    │
│      - 密钥文件 = 加密后的密钥 + salt                          │
│      - 新设备可导入密钥文件，不需要知道密码原文                  │
│      - 密钥文件本身需要保护密码                                │
│                                                             │
│  防暴力破解:                                                   │
│    • PBKDF2 10 万轮 ──► 每次尝试成本高                         │
│    • 客户端本地，远程攻击者接触不到                              │
│    • 即使拿到注册表 ──► 也只能拿到密钥，不是密码原文             │
│                                                             │
└─────────────────────────────────────────────────────────────┘
```

### 11.6 安全清单

| 安全维度 | 措施 | 防护目标 |
|---------|------|---------|
| **登录暴力破解** | IP 5 次/分 + 用户 10 次/小时 + bcrypt | 防止密码爆破 |
| **重启绕过** | 限流计数器持久化到 SQLite | 重启不丢失 |
| **Token 盗用** | 设备可撤销 + 90 天过期 | 限制泄露影响范围 |
| **端到端密码** | PBKDF2 10 万轮 + 不存明文 | 防止本地窃取 |
| **传输安全** | HTTPS/TLS 1.2+ | 防止中间人窃听 |
| **密码存储** | bcrypt cost=12 | 数据库泄露也无法反推 |
| **错误信息** | 不区分"用户不存在"和"密码错" | 防止用户名枚举 |
| **文件不同步** | CF_HDROP 只同步路径 | 防止大文件/隐私泄露 |

### 11.7 与 Ditto 现有加密的关系

```
┌──────────────────┬───────────────────────┬────────────────────┐
│      维度        │   Ditto 现有           │   云端方案          │
├──────────────────┼───────────────────────┼────────────────────┤
│ LAN 同步         │ 共享密码 + AES-256-CBC│ 保持不变            │
│ 云端传输         │ N/A                   │ HTTPS/TLS           │
│ 云端存储         │ N/A                   │ 端到端 AES-256-GCM  │
│ 密码派生         │ SHA-256 + 10万轮      │ PBKDF2 10万轮       │
│ 云端能看到       │ N/A（本地存储）        │ 仅密文 + 元数据      │
└──────────────────┴───────────────────────┴────────────────────┘
```

---

## 十二、增量同步设计

### 12.1 同步流程

```
┌─────────┐                      ┌─────────┐
│ Ditto客户端 │                      │  Go 服务端 │
│ (设备 A)   │                      │  (统一仓库) │
└────┬─────┘                      └────┬────┘
     │                                 │
     │  POST /clips/sync               │
     │  { clips: [...], since: "..." } │
     │ ──────────────────────────────► │
     │                                 │
     │  1. CRC 去重（user_id 维度）     │
     │  2. 事务入库（统一仓库）          │
     │  3. 大文件上传 MinIO             │
     │                                 │
     │  { synced: 3, skipped: 1 }      │
     │ ◄────────────────────────────── │
     │                                 │
     │  GET /clips/changes?since=...   │
     │ ──────────────────────────────► │
     │                                 │
     │  1. WHERE user_id = ?           │
     │     AND updated_at > since      │
     │     AND source_device != A      │
     │  2. 返回所有设备的变更            │
     │                                 │
     │  { clips: [...], deleted: [...] }│
     │ ◄────────────────────────────── │
     │                                 │
     │  本地合并入库                    │
```

**统一仓库的关键语义**：
- `POST /clips/sync` → 推入用户共享仓库
- `GET /clips/changes` → 拉取**所有设备**的变更（不只是自己的）
- CRC 去重在 `user_id` 维度，跨设备也去重
- 删除是全局的，一个设备删除 = 所有设备不可见

### 12.2 冲突解决策略

| 场景 | 策略 |
|------|------|
| CRC 相同 | 跳过（视为同一条） |
| 同一远程 ID 内容不同 | 服务端最后写入优先 (LWW) |
| 删除冲突 | 软删除优先（tombstone） |
| 大文件冲突 | 保留多版本 |

---

## 十三、部署方案

### 13.1 Docker Compose（唯一方案，推荐）

```yaml
# docker-compose.yml
version: '3.8'
services:
  api:
    build: .
    ports:
      - "8080:8080"
    volumes:
      - ./data:/app/data   # SQLite DB 持久化
    environment:
      - DB_PATH=/app/data/ditto_cloud.db
      - JWT_SECRET=your-secret-key-change-me
    restart: unless-stopped

  web:
    build: ./ditto-cloud-web
    ports:
      - "3000:80"
    depends_on:
      - api
    restart: unless-stopped
```

> 仅需 2 个容器。SQLite 文件挂载到 `./data/ditto_cloud.db`，备份 = 拷贝文件。

### 13.2 直接二进制运行（无 Docker）

```bash
# 下载编译好的二进制
./ditto-cloud-server --db-path ./ditto.db --port 8080
```

### 13.3 生产扩展（可选）

如需多用户高并发场景：
- GORM 代码层无差异，改配置即可切换 PostgreSQL
- 需要额外部署 PostgreSQL + Redis

---

## 十四、开发计划

### 策略：三个项目并行推进

```
┌──────────────────────────────────────────────────────────────────┐
│                     并行开发架构                                   │
│                                                                  │
│  项目 A: Go 后端          项目 B: Web 前端      项目 C: C++ 客户端│
│  ───────────────────     ─────────────────     ─────────────────  │
│  Phase 1: 基础 API        Phase 1: 基础界面     Phase 1: 框架搭建  │
│       ↓                        ↓                      ↓          │
│  Phase 2: 高级功能        Phase 2: 完善界面       Phase 2: 认证   │
│       ↓                        ↓                      ↓          │
│  Phase 3: 集成测试        Phase 3: 联调          Phase 3: 同步   │
│       ↘                       ↙                      ↙           │
│        └─────────────────────┴──────────────────────┘            │
│                                                                  │
│  接口契约:                                                        │
│    • API 文档（Swagger）作为三方共同遵守的契约                     │
│    • 前端和客户端先用 Mock 数据开发                               │
│    • 后端就绪后联调                                               │
│                                                                  │
│  依赖关系:                                                        │
│    • 项目 B（Web） 依赖 项目 A 的 API                             │
│    • 项目 C（C++） 依赖 项目 A 的 API                             │
│    • 项目 A 可以独立开发测试                                     │
│                                                                  │
└──────────────────────────────────────────────────────────────────┘
```

### 阶段 1: 基础设施搭建（Week 1-2）

#### 项目 A: Go 后端

| 任务 | 产出 | 优先级 |
|------|------|--------|
| 项目初始化 + GORM + SQLite | 可运行的 Go 服务 | P0 |
| 数据库迁移（7 张表） | 自动建表 | P0 |
| 用户注册/登录 + JWT | `/auth/*` 接口 | P0 |
| 设备注册/管理 | `/devices/*` 接口 | P0 |
| 剪贴板 CRUD | `/clips/*` 基础接口 | P0 |
| 增量同步 API | `/clips/changes`, `/clips/sync` | P0 |
| Swagger 文档 | 自动生成 API 文档 | P1 |

#### 项目 B: Web 前端

| 任务 | 产出 | 优先级 |
|------|------|--------|
| Vue 3 项目初始化 | 可运行的中文 Web 应用 | P0 |
| 登录/注册页面 | 认证界面 | P0 |
| API 请求封装（Axios） | 拦截器 + Token 管理 | P0 |
| Mock 数据开发 | 无后端也可预览界面 | P1 |

#### 项目 C: C++ 客户端

| 任务 | 产出 | 优先级 |
|------|------|--------|
| 引入 `httplib.h` + `json.hpp` | 第三方库集成 | P0 |
| `CloudAuth.h/cpp` 框架 | 认证模块骨架 | P0 |
| `CloudSyncManager.h/cpp` 框架 | 同步管理器骨架 | P0 |
| `OptionCloud.h/cpp` 骨架 | 设置 Tab 骨架 | P0 |
| 注册表配置读写 | `Options.h` 新增云端配置项 | P0 |

---

### 阶段 2: 核心功能开发（Week 3-4）

#### 项目 A: Go 后端

| 任务 | 产出 | 优先级 |
|------|------|--------|
| WebSocket 实时推送 | `/ws/sync`（Go channel 广播） | P0 |
| 端到端加密支持 | Salt 管理 + AES-256-GCM 工具包 | P0 |
| 内存限流中间件 | 每 IP 5 次/分 + 持久化到 SQLite | P0 |
| 请求日志中间件 | zap 结构化日志 | P1 |
| 错误码规范 | 统一错误响应 | P0 |

#### 项目 B: Web 前端

| 任务 | 产出 | 优先级 |
|------|------|--------|
| 剪贴板列表页 | 分页、搜索、过滤 | P0 |
| 剪贴板详情弹窗 | 多格式预览（文本/图片/HTML） | P0 |
| WebSocket 实时通知 | 自动刷新列表 | P0 |
| Token 自动刷新 | Axios 拦截器 | P0 |

#### 项目 C: C++ 客户端

| 任务 | 产出 | 优先级 |
|------|------|--------|
| `CloudAuth::Login()` | 完整登录流程 | P0 |
| 设备 Token 持久化 | 注册表存储 | P0 |
| `CloudSyncManager::PushNewClips()` | 推送逻辑（明文） | P0 |
| `CloudSyncManager::PullChanges()` | 拉取逻辑（明文） | P0 |
| 后台同步线程 | 参考 `AutoSendToClientThread` | P0 |

---

### 阶段 3: 集成与完善（Week 5-6）

#### 项目 A: Go 后端

| 任务 | 产出 | 优先级 |
|------|------|--------|
| 三方联调 | 修复 API 问题 | P0 |
| 端到端加密集成 | 客户端加密流程验证 | P0 |
| 自动清理任务 | goroutine 定时执行 | P1 |
| 单元测试 | 覆盖率 > 60% | P1 |

#### 项目 B: Web 前端

| 任务 | 产出 | 优先级 |
|------|------|--------|
| 仪表盘页面 | 统计图表 | P1 |
| 分组管理页面 | 树形分组操作 | P1 |
| 设备管理页面 | 设备列表/移除 | P1 |
| 设置页面 | 端到端密码管理 | P0 |
| 联调真实 API | 替换 Mock 数据 | P0 |

#### 项目 C: C++ 客户端

| 任务 | 产出 | 优先级 |
|------|------|--------|
| 端到端加密集成 | AES-256-GCM（复用 Ditto 现有或重写） | P0 |
| 配置界面 | Options "云端同步" Tab | P0 |
| 冲突处理逻辑 | LWW 策略 | P1 |
| 文件类型处理 | CF_HDROP 只同步路径 | P0 |
| 联调真实 API | 替换 Mock 数据 | P0 |

---

### 阶段 4: 完整闭环（Week 7-8）

| 项目 | 任务 | 产出 |
|------|------|------|
| **三方** | 端到端加密全流程测试 | 数据密文存储验证 |
| **三方** | 多设备同步测试 | A 复制 B 能看到 |
| **A** | 性能优化 | SQLite 索引调优 |
| **B** | 响应式适配 | 移动端可用 |
| **C** | 异常处理 | 网络断开/Token 过期/密码错误 |
| **C** | 密钥文件导出 | `.dittokey` 备份 |
| **文档** | 部署文档 | Docker 部署指南 |
| **文档** | 用户手册 | 中文使用说明 |

---

### 关键里程碑

```
Week 1    Week 2    Week 3    Week 4    Week 5    Week 6    Week 7    Week 8
  │         │         │         │         │         │         │         │
  ▼         ▼         ▼         ▼         ▼         ▼         ▼         ▼
项目骨架   基础API   核心功能  核心功能  联调     联调     闭环     发布
  │         │         │         │         │         │         │         │
  ├─ Go     ├─ JWT    ├─ WS     ├─ 加密   ├─ 联调  ├─ Web   ├─ 端到端  ├─ v0.1
  ├─ Vue    ├─ CRUD   ├─ 限流   ├─ 推送   ├─ Mock  ├─ 设置  ├─ 多设备  ├─ Release
  └─ C++    └─ DB     └─ 线程   └─ 明文   └─ 真实  └─ 文件  └─ 异常   └─ 文档
```

### 并行开发协调规则

1. **API 契约优先**: 后端先定义 Swagger，前端和客户端按契约开发
2. **Mock 数据**: 前端和客户端先用 Mock，不阻塞开发
3. **每日同步**: 三方每日提交进度，阻塞问题当日解决
4. **接口变更**: 任何 API 变更必须通知三方，更新 Swagger 文档

---

## 十五、关键技术决策记录 (ADR)

### ADR-001: 为什么新增 JWT 认证而不是复用 Ditto 共享密码？

**决策**: 采用 JWT + 用户账号体系

**理由**:
1. Ditto 无用户概念，共享密码仅为 LAN 加密设计
2. 默认密码 `"LetMeIn"` 是公开硬编码，不适合作为云端凭据
3. 多租户隔离必须依赖用户身份
4. JWT 支持设备级 Token 管理（可单独撤销）
5. 行业标准，便于后续 OAuth2/SSO 集成

**影响**: Ditto 客户端需新增登录流程，可记忆 Token 减少摩擦

### ADR-002: 数据库选型

**决策**: SQLite3 为主数据库

**理由**:
1. 个人使用场景（1-5 用户），并发量极低
2. 零部署成本，单文件即可运行
3. 备份 = 拷贝文件，极其简单
4. GORM 完全支持，代码层与 PostgreSQL 无差异
5. 未来如需扩展可随时切换 PostgreSQL

**限制**:
- SQLite 同一时刻只允许 1 个写操作
- WAL 模式可缓解但仍有上限
- 多用户高并发场景不适用

### ADR-003: 大附件存储

**决策**: 全部存 SQLite BLOB，端到端加密后存储

**理由**:
1. 去掉 MinIO，部署从 4 个容器减少到 2 个
2. 个人使用剪贴板数据量有限（通常 < 500MB）
3. 端到端加密后存 BLOB，安全性不减
4. SQLite 单文件备份，运维极简
5. 如 DB 膨胀到 GB 级，可加自动清理策略

### ADR-004: 端到端加密

**决策**: 默认开启，客户端加密后上传

**理由**:
1. 云端存储的是密文，服务端无法窥探
2. 复用 Ditto 现有加密理念（AES-256）
3. 用户体验低摩擦——只需设一次密码
4. 密钥派生: PBKDF2(用户密码 + salt) → AES-256-GCM

**影响**:
- Ditto 客户端必须集成加密逻辑
- 忘记密码 = 数据不可恢复（需提醒用户）

### ADR-005: 去掉 Redis + 限流持久化

**决策**: 用 Go 内存替代 Redis，但限流计数器持久化到 SQLite

**理由**:
1. 单实例部署，无需分布式
2. WebSocket 广播用 Go channel 即可
3. 会话用内存 + JWT 自包含

**问题**: 服务重启后内存限流计数器归零，攻击者可利用此绕过暴力破解防护

**解决方案**: 限流计数器写 SQLite 小表

```sql
-- 限流记录表（轻量，不影响核心功能）
CREATE TABLE rate_limit_records (
    key         TEXT PRIMARY KEY,     -- "ip:1.2.3.4" 或 "user:username"
    fail_count  INTEGER DEFAULT 0,
    ban_until   DATETIME,
    updated_at  DATETIME DEFAULT CURRENT_TIMESTAMP
);
```

```go
// 启动时从 SQLite 加载限流记录到内存
func (rl *RateLimiter) LoadFromDB(db *gorm.DB) {
    var records []RateLimitRecord
    db.Where("ban_until > ?", time.Now()).Find(&records)
    for _, r := range records {
        rl.ipBanUntil[r.key] = r.banUntil
        rl.ipFailCount[r.key] = r.failCount
    }
}

// 每次更新时写库（写操作极少，不影响性能）
func (rl *RateLimiter) RecordFailureToDB(db *gorm.DB, key string) {
    db.Exec("INSERT OR REPLACE INTO rate_limit_records (key, fail_count, ban_until, updated_at) VALUES (?, ?, ?, ?)",
        key, rl.ipFailCount[key], rl.ipBanUntil[key], time.Now())
}
```

**代价权衡**:
- 写库频率极低（仅登录失败时触发），不是瓶颈
- 重启后限流状态保留，攻击者无法通过重启绕过
- 正常用户无感知

### ADR-006: 冲突解决策略

**决策**: 默认 LWW (Last Write Wins) + CRC 去重

**理由**:
1. 剪贴板场景冲突率低（通常不会产生同 ID 不同内容）
2. CRC 去重避免同一内容重复存储
3. LWW 实现简单，性能最优
4. 保留冲突日志供审计

---

## 十六、风险与对策

| 风险 | 影响 | 对策 |
|------|------|------|
| 端到端加密密码遗忘 | 高 | 明确警告 + 可选导出密钥文件 |
| SQLite DB 文件损坏 | 高 | 定期自动备份 + WAL 模式 |
| 同步冲突导致数据丢失 | 高 | CRC 去重 + 软删除 + 同步日志可追溯 |
| DB 文件膨胀到 GB 级 | 中 | 自动清理策略 + 存储配额 |
| WebSocket 连接不稳定 | 低 | 自动重连 + HTTP 轮询兜底 |
| 客户端集成复杂度 | 中 | 参考现有 AutoSendToClientThread 架构 |

---

## 十七、附录

### 参考文件

| 文件 | 说明 |
|------|------|
| `src/DatabaseUtilities.cpp` | 数据库建表 SQL 和工具函数 |
| `src/Clip.cpp`, `src/Clip.h` | CClip 数据模型 |
| `src/Server.cpp`, `src/Client.cpp` | 现有 P2P 同步实现 |
| `src/ServerDefines.h` | 同步协议定义 |
| `src/Options.h` | 配置项定义 |
| `EncryptDecrypt/Encryption.cpp` | 现有加密算法实现 |
| `src/AutoSendToClientThread.cpp` | 后台同步线程参考 |

### 关键数据流

```
用户在设备 A 复制内容
    │
    ▼
Ditto 监控到剪贴板变更
    │
    ▼
CClip::SaveToDatabase() ──► 本地 SQLite（明文）
    │
    ▼
CloudSyncManager::OnClipAdded()
    │
    ▼
AES-256-GCM 加密（端到端）
    │
    ▼
CCloudSyncClient::POST /clips/sync ──► 云端 SQLite（密文）
    │                                        │
    │                                        ▼
    │                                 WebSocket 推送
    │                                        │
    │                                   设备 B 收到通知
    │                                        │
    │                                        ▼
    │                          GET /clips/changes（拉取密文）
    │                                        │
    │                                        ▼
    │                          AES-256-GCM 解密（本地）
    │                                        │
    │                                        ▼
    │                          写入本地 SQLite（明文）
    ▼
```

**统一仓库保证**：
- 设备 A 推送的内容 → 设备 B/C 都能拉取到
- 设备 B 删除的内容 → 设备 A/C 都不可见
- Web 端看到的是所有设备的完整历史
- **云端只看到密文，无法解密**

---

*文档版本: v2.9 - Playwright 前端 E2E 测试通过*
*创建日期: 2025-04-07*
*更新日期: 2025-04-07*
*基于: Ditto v3.25 代码库分析*

**v2.9 变更**:
- 新增 Playwright E2E 测试框架（Chromium 浏览器）
- 12 个前端 E2E 测试全部通过（20 秒内完成）
- 修复前端 API 路径：/api/auth/* → /api/v1/auth/*
- 修复前端响应码判断：code 200 → code 0
- 修复登录响应字段：token → device_token
- 覆盖：注册、登录、错误处理、登出、表单验证、剪贴板 CRUD、导航、Token 持久化
- 测试自动启动后端和前端服务器
