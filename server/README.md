# Ditto Cloud Server

Go 后端服务，为 Ditto 剪贴板管理器提供云端同步功能。

## 功能特性

- ✅ 用户注册/登录 + JWT 认证
- ✅ 设备管理（多设备同步）
- ✅ 剪贴板 CRUD + 增量同步
- ✅ WebSocket 实时推送
- ✅ 端到端加密（AES-256-GCM）
- ✅ 暴力破解防护（IP + 用户维度限流）
- ✅ SQLite3 持久化（默认）
- ⚠️ PostgreSQL 尚未接入：`DATABASE_DRIVER=postgres` 会直接报错退出，需先在 go.mod 引入 `gorm.io/driver/postgres` 并实现 `InitPostgres`

## 快速启动

### 开发环境

```bash
# 1. 安装依赖
go mod download

# 2. 复制环境配置
cp .env.example .env

# 3. 启动服务（默认端口 8080）
go run cmd/server/main.go

# 或使用 Make
make run
```

### 生产环境（Docker）

```bash
# 构建镜像
docker build -t ditto-cloud-server .

# 运行
docker run -d -p 8080:8080 \
  -v ./data:/app/data \
  -e DB_PATH=/app/data/ditto_cloud.db \
  -e JWT_SECRET=your-32-bytes-or-longer-secret-here \
  ditto-cloud-server
```

### 生产环境（Docker Compose + TLS）

```bash
docker-compose -f docker-compose.prod.yml up -d
```

## 项目结构

```
server/
├── cmd/server/main.go          # 入口
├── cmd/cli/                    # CLI 工具入口
├── internal/
│   ├── handler/                # HTTP 处理器
│   ├── service/                # 业务逻辑
│   ├── model/                  # 数据模型
│   ├── middleware/             # JWT + 限流中间件
│   ├── hub/                    # WebSocket 连接管理
│   ├── database/               # 数据库初始化
│   ├── config/                 # Viper 配置
│   ├── response/               # 统一响应格式
│   └── utils/                  # 工具函数
├── pkg/crypto/                 # 加密工具（AES-256-GCM, PBKDF2）
├── migrations/                 # 数据库迁移
└── api/swagger.yaml            # API 文档
```

## API 文档

启动服务后访问：`http://localhost:8080/swagger/index.html`

### 核心接口

| 方法 | 路径 | 说明 |
|------|------|------|
| POST | `/api/v1/auth/register` | 用户注册 |
| POST | `/api/v1/auth/login` | 登录 |
| POST | `/api/v1/clips/sync` | 批量同步推送 |
| GET | `/api/v1/clips/changes?since=...` | 增量拉取变更 |
| GET | `/api/v1/clips` | 剪贴板列表 |
| DELETE | `/api/v1/clips/:id` | 删除剪贴板 |
| GET | `/api/v1/devices` | 设备列表 |
| DELETE | `/api/v1/devices/:id` | 移除设备 |
| GET | `/api/v1/stats/overview` | 统计概览 |
| WS | `/ws/sync` | 实时同步推送 |

## 配置说明

配置通过 `.env` 文件管理，参考 `.env.example`：

```bash
# .env
PORT=8080
DATABASE_PATH=/app/data/ditto_cloud.db
# 至少 32 字节，否则服务启动即退出：openssl rand -hex 32
JWT_SECRET=change-me-to-openssl-rand-hex-32-output
JWT_ACCESS_TOKEN_EXPIRY=15
JWT_REFRESH_TOKEN_EXPIRY=10080

# 限流配置
RATE_LIMIT_LOGIN=5
RATE_LIMIT_API=60
RATE_LIMIT_BAN_DURATION=900
```

## 数据库迁移

服务启动时自动迁移（GORM AutoMigrate）。手动执行：

```bash
# 查看 migrations/001_init.sql 了解表结构
sqlite3 data/ditto_cloud.db < migrations/001_init.sql
```

## 限流管理

### 查看封禁记录

```bash
sqlite3 data/ditto_cloud.db "SELECT * FROM rate_limit_records;"
```

### 重置限流

```bash
# 重置指定用户
sqlite3 data/ditto_cloud.db "DELETE FROM rate_limit_records WHERE key = 'user:zhangsan';"

# 重置指定 IP
sqlite3 data/ditto_cloud.db "DELETE FROM rate_limit_records WHERE key = 'ip:192.168.1.100';"

# 清空所有限流记录
sqlite3 data/ditto_cloud.db "DELETE FROM rate_limit_records;"
```

## 测试

```bash
# 单元测试
go test ./... -v

# WebSocket 测试
go test ./internal/hub -v
```

## 技术栈

| 组件 | 技术 |
|------|------|
| Web 框架 | Gin |
| ORM | GORM |
| WebSocket | gorilla/websocket |
| JWT | golang-jwt/jwt |
| 数据库 | SQLite3（PostgreSQL 尚未接入） |
| 加密 | crypto/aes + golang.org/x/crypto/pbkdf2 |
| 日志 | zap |
| 配置 | viper |

## 许可证

GPL-3.0
