# AGENTS.md — Ditto 剪贴板管理器

## 项目概览

多语言仓库：**C++/MFC**（Windows 桌面客户端）+ **Go**（云端后端）+ **Vue 3/Vite**（Web 管理面板）。

`src/` 是主程序（~130 个 .cpp/.h，Visual Studio 2022 解决方案 `CP_Main_10.sln`），`server/` 是 Go 后端，`web/` 是前端。

## 开发者命令

### Go 后端 (`server/`)
```bash
cd server && make build     # 编译到 bin/server
cd server && make run       # 开发模式运行
cd server && make test      # 全部测试 + 覆盖率
cd server && go test ./... -v -cover  # 同上
```

### Web 前端 (`web/`)
```bash
cd web && npm install
cd web && npm run dev              # Vite 开发服务器 (默认 :5173)
cd web && npm run test:unit        # Vitest 单元测试
cd web && npm run test:e2e         # Playwright E2E 测试
cd web && npm run build            # 生产构建
```

### E2E 集成测试
```bash
scripts/test-e2e.sh  # 启动 Go 后端 -> 健康检查 -> 注册 -> 登录 -> API 测试 -> 清理
```

## 架构要点

| 层 | 目录 | 技术 |
|---|---|---|
| 桌面客户端 | `src/` | C++/MFC, Unicode, Visual Studio 2022 |
| 云端后端 | `server/` | Go 1.22, Gin, GORM, gorilla/websocket |
| 管理面板 | `web/` | Vue 3, Element Plus, Pinia, Axios |
| 部署 | `docker/` | Docker Compose (dev/prod/postgres) |

- **认证**: JWT (Access Token 15min + Refresh Token 7天), HttpOnly cookie
- **加密**: AES-256-GCM + PBKDF2 10万轮, 密钥文件 `.dittokey`
- **同步**: WebSocket 实时推送, CRC 去重, LWW 冲突解决
- **数据库**: 默认 SQLite, 可选 PostgreSQL (docker-compose.postgres.yml)
- **测试**: Go 端 29 个测试用例, 使用 httptest; C++ 端 Google Test

## 关键约束

- **C++ 代码无法在此环境编译**（Linux，无 MSVC/MFC）
- 修改 `src/` 或 `Addins/` 中的 C++ 代码时，仅做不影响编译的改动（如注释、文档、轻量重构）
- 所有 `docker/` 内的 Compose 文件已更新内部路径，勿回退到根目录
- 首次部署需 `cp .env.example .env` 并设置 `JWT_SECRET`

## 目录结构

```
src/          — C++ 主程序 (Win32/x64/ARM64, Debug/Release)
Addins/       — 插件 (DittoOCR, DittoUtil)
DittoSetup/   — 安装程序 (Inno Setup, FinalBuilder)
server/       — Go 后端 (cmd/server/ 入口, internal/ 业务逻辑, tests/ 测试)
web/          — Vue 3 前端 (src/views/ 页面, tests/e2e/ Playwright)
docker/       — Docker Compose 配置
scripts/      — 构建/测试/证书脚本
tools/        — 辅助工具 (CloudSync_Test, EncryptDecrypt 等)
docs/         — 文档 (部署指南, 架构设计, 用户手册)
```

## 测试须知

- Go 测试无需外部依赖，直接 `go test ./...` 即可
- Web E2E 测试需要先启动 Go 后端（`make run` 或 `docker-compose up -d`）
- E2E 集成测试脚本会自行启动/停止 Go 后端
- 测试管理员账号通过环境变量 `ADMIN_USERNAME`/`ADMIN_PASSWORD` 首次自动创建

## 文档参考

- `docs/PROJECT_PLAN.md` — 完整架构设计与 API 文档
- `docs/DEPLOYMENT_GUIDE.md` — 部署步骤
- `docs/CLOUD_DEPLOYMENT.md` — 云部署详解
- `server/tests/README.md` — Go 测试说明