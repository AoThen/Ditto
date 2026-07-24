# Quick Deployment Guide | 快速部署指南

**[English](#table-of-contents)** | **[中文](#目录中文版)**

> 最后审阅日期：2026-07-24（内容基于当前 Docker Compose 部署架构）

---

# 目录中文版

本指南提供快速部署 Ditto 云端服务器的说明，适用于各种部署场景。

## 5 分钟快速开始

### 前提条件

- 已安装 Docker 20.10+ 和 Docker Compose 2.0+
- 已安装 Git
- Linux/macOS/WSL 终端访问权限

### 快速命令

```bash
# 1. 克隆仓库（或使用现有的 Ditto 源代码）
git clone https://github.com/sabrogden/Ditto.git
cd Ditto

# 2. 配置环境
cp .env.example .env
JWT_SECRET=$(openssl rand -base64 32)
sed -i "s/change-me-in-production/$JWT_SECRET/" .env

# 3. 启动开发环境
docker-compose up -d

# 4. 访问服务
echo "Web 面板 + API: http://localhost:8080"
echo "健康检查: http://localhost:8080/health"

# 5. 查看日志
docker-compose logs -f
```

**完成！** 您现在已在本地运行 Ditto 云端服务。

### 初始化管理员账号

部署完成后，通过以下三种方式之一创建管理员账号：

**方式一：环境变量自动创建（推荐）**

在 `.env` 文件中设置管理员信息，首次启动时自动创建：
```bash
ADMIN_USERNAME=admin
ADMIN_PASSWORD=your-secure-password
ADMIN_EMAIL=admin@example.com
```
创建成功后建议注释或删除这三行。

**方式二：首用户注册**

数据库为空时，第一个通过客户端或 Web 注册的用户自动成为管理员。之后注册功能自动关闭。

**方式三：CLI 工具**

进入容器后使用 CLI 创建：
```bash
docker exec -it ditto-backend /app/cli create-admin
```
同样支持 `reset-password`（重置密码）和 `list-users`（列出用户）。

## 开发环境部署（HTTP）

**适用于**: 开发、测试、本地访问

```bash
# 启动服务
docker-compose up -d

# 验证部署
curl http://localhost:8080/health

# 预期响应: {"status":"ok","timestamp":"..."}

# 访问 Web 管理面板: http://localhost:8080
```

**使用端口:**
- Web 面板 + API: 8080（单端口，Go 统一输出）

**停止服务:**
```bash
docker-compose down
```

**重置数据（⚠️ 将删除所有数据）:**
```bash
docker-compose down -v
```

## 生产环境部署（单服务器 HTTPS）

**适用于**: 生产环境、远程访问、多用户

### 步骤 1: 准备服务器

```bash
# 更新系统
sudo apt update && sudo apt upgrade -y

# 安装 Docker
curl -fsSL https://get.docker.com -o get-docker.sh
sudo sh get-docker.sh
sudo usermod -aG docker $USER
newgrp docker
```

### 步骤 2: 克隆和配置

```bash
cd /opt
sudo git clone https://github.com/sabrogden/Ditto.git
cd Ditto

# 配置环境
cp .env.example .env
JWT_SECRET=$(openssl rand -base64 32)
sed -i "s/change-me-in-production/$JWT_SECRET/" .env

# 验证配置
cat .env | grep JWT_SECRET
```

### 步骤 3: 生成 SSL 证书

**选项 A: 自签名证书（仅测试用）**

```bash
chmod +x generate-certs.sh
./generate-certs.sh "ditto.local"
```

**选项 B: Let's Encrypt 证书（生产环境）**

```bash
# 安装 Certbot
sudo apt install -y certbot

# 停止 80 端口上的任何服务
sudo systemctl stop nginx 2>/dev/null || true

# 获取证书
sudo certbot certonly --standalone -d ditto.your-domain.com

# 复制证书
mkdir -p certs
sudo cp /etc/letsencrypt/live/ditto.your-domain.com/fullchain.pem certs/server.crt
sudo cp /etc/letsencrypt/live/ditto.your-domain.com/privkey.pem certs/server.key
sudo chmod 644 certs/server.crt
sudo chmod 600 certs/server.key
```

### 步骤 4: 启动 HTTPS 服务

```bash
# 启动生产环境（单容器，Go 统一输出 API + 前端静态文件）
docker-compose -f docker-compose.prod.yml up -d

# 验证部署
docker-compose -f docker-compose.prod.yml ps

# 测试 HTTPS 端点
curl -k https://localhost/health
```

### 步骤 5: 配置防火墙

```bash
# 启用 UFW
sudo ufw enable

# 允许必要的端口
sudo ufw allow 22/tcp   # SSH
sudo ufw allow 80/tcp   # HTTP（用于 certbot 续期）
sudo ufw allow 443/tcp  # HTTPS

# 验证
sudo ufw status
```

### 步骤 6: 设置自动重启

```bash
# 创建 systemd 服务
sudo tee /etc/systemd/system/ditto-cloud.service > /dev/null <<EOF
[Unit]
Description=Ditto Cloud Server
Requires=docker.service
After=docker.service

[Service]
Type=oneshot
RemainAfterExit=yes
WorkingDirectory=/opt/Ditto
ExecStart=/usr/bin/docker compose -f docker-compose.prod.yml up -d
ExecStop=/usr/bin/docker compose -f docker-compose.prod.yml down
TimeoutStartSec=0

[Install]
WantedBy=multi-user.target
EOF

# 启用并启动
sudo systemctl enable ditto-cloud.service
sudo systemctl start ditto-cloud.service

# 验证
sudo systemctl status ditto-cloud.service
```

## 云服务商部署

### AWS EC2

```bash
# 1. 启动 Ubuntu 22.04 EC2 实例（推荐 t3.small 或更高）
# 2. 配置安全组:
#    - SSH (22): 您的 IP
#    - HTTP (80): 0.0.0.0/0
#    - HTTPS (443): 0.0.0.0/0

# 3. SSH 连接到实例
ssh -i your-key.pem ubuntu@<public-ip>

# 4. 部署（与单服务器步骤相同）
curl -fsSL https://get.docker.com -o get-docker.sh
sudo sh get-docker.sh
sudo usermod -aG docker $USER

git clone https://github.com/sabrogden/Ditto.git
cd Ditto
cp .env.example .env
JWT_SECRET=$(openssl rand -base64 32)
sed -i "s/change-me-in-production/$JWT_SECRET/" .env

# 5. 获取 Let's Encrypt 证书
sudo apt install -y certbot
sudo certbot certonly --standalone -d ditto.your-domain.com

mkdir -p certs
sudo cp /etc/letsencrypt/live/ditto.your-domain.com/fullchain.pem certs/server.crt
sudo cp /etc/letsencrypt/live/ditto.your-domain.com/privkey.pem certs/server.key

# 6. 启动服务
docker-compose -f docker-compose.prod.yml up -d
```

### DigitalOcean Droplet

```bash
# 1. 创建 Ubuntu 22.04 Droplet（推荐 $12/月 配置）
# 2. 在创建时添加您的 SSH 密钥
# 3. 在 DigitalOcean 网络面板中添加域名
# 4. SSH 连接到 Droplet
ssh root@<droplet-ip>

# 5. 部署（与上述步骤相同）
```

## 客户端配置

### Ditto Windows 客户端配置

部署云端服务器后，配置您的 Ditto 桌面客户端：

#### 方法 1: 通过设置界面

1. 打开 Ditto
2. 右键点击系统托盘图标 → 选项
3. 导航到"云端同步"选项卡
4. 输入服务器 URL: `https://ditto.your-domain.com`
5. 点击"登录"并输入凭据
6. 启用"启动时同步"

#### 方法 2: 通过注册表

```
Windows Registry Editor Version 5.00

[HKEY_CURRENT_USER\Software\Ditto\CloudSync]
"ServerUrl"="https://ditto.your-domain.com"
"DeviceToken"="<从 Web 面板获取的令牌>"
"SyncEnabled"=dword:00000001
"SyncInterval"=dword:0000001e
"AutoDeleteDays"=dword:0000005a
```

#### 方法 3: 通过 INI 文件（便携版）

编辑 `Ditto.ini`:

```ini
[CloudSync]
ServerUrl=https://ditto.your-domain.com
DeviceToken=<从 Web 面板获取的令牌>
SyncEnabled=1
SyncInterval=30
AutoDeleteDays=90
```

## 常见部署场景

### 场景 1: 个人使用（单用户，单设备）

```bash
# 在小型 VPS 上最小化部署（$5-10/月）
# - SQLite 数据库
# - 自签名或 Let's Encrypt 证书
# - 单个后端实例

docker-compose -f docker-compose.prod.yml up -d
```

**推荐基础设施:**
- DigitalOcean Droplet: $6/月（1 GB 内存，1 CPU）
- 域名: ~$10/年
- **总计: ~$11/月**

### 场景 2: 小团队（5-20 用户）

```bash
# 添加 PostgreSQL 以获得更好的并发性能
docker-compose -f docker-compose.prod.yml \
  -f docker-compose.postgres.yml \
  up -d
```

**推荐基础设施:**
- DigitalOcean Droplet: $18/月（2 GB 内存，2 CPU）
- 域名: ~$10/年
- **总计: ~$19/月**

### 场景 3: 企业（100+ 用户）

```bash
# 完整堆栈，包含负载均衡、PostgreSQL、监控
docker-compose -f docker-compose.prod.yml \
  -f docker-compose.postgres.yml \
  -f docker-compose.monitoring.yml \
  up -d
```

**推荐基础设施:**
- AWS: 2x t3.medium（$30/月 每台）+ RDS PostgreSQL（$50/月）
- Application Load Balancer（$20/月）
- **总计: ~$130/月**

## 部署后检查清单

### ✅ 基础设施

- [ ] 服务器操作系统已更新
- [ ] Docker 和 Docker Compose 已安装
- [ ] 防火墙已配置（端口 22、80、443）
- [ ] SSL 证书已安装且有效
- [ ] 自动重启服务已配置
- [ ] 备份策略已实施

### ✅ 应用程序

- [ ] 环境变量已配置（`.env` 文件）
- [ ] JWT 密钥唯一且安全
- [ ] 管理员账号已初始化（环境变量 / CLI / 首用户注册）
- [ ] 服务正在运行（`docker-compose ps`）
- [ ] 健康检查通过（`curl https://localhost/health`）
- [ ] 日志可访问且无错误（`docker-compose logs`）
- [ ] Web 面板可通过 HTTPS 访问

### ✅ 安全

- [ ] 默认密码已更改
- [ ] 启用 SSH 密钥认证
- [ ] Fail2Ban 已安装和配置
- [ ] 自动安全更新已启用
- [ ] 速率限制已配置
- [ ] CORS 策略已设置（如适用）

### ✅ 客户端设置

- [ ] Ditto 客户端已配置服务器 URL
- [ ] 用户账号已创建
- [ ] 设备令牌已生成
- [ ] 已在设备间测试同步
- [ ] 端到端加密已启用

## 有用的命令参考

```bash
# 启动服务
docker-compose -f docker-compose.prod.yml up -d

# 停止服务
docker-compose -f docker-compose.prod.yml down

# 重启特定服务
docker-compose -f docker-compose.prod.yml restart backend

# 查看日志
docker-compose logs -f backend

# 查看资源使用情况
docker stats

# 访问后端 Shell
docker exec -it ditto-backend sh

# 访问数据库
docker exec -it ditto-backend sqlite3 /app/data/ditto_cloud.db

# 备份数据库
docker exec ditto-backend sqlite3 /app/data/ditto_cloud.db ".backup /tmp/backup.db"
docker cp ditto-backend:/tmp/backup.db ./backup-$(date +%Y%m%d).db

# 更新服务
cd /opt/Ditto
git pull
docker-compose -f docker-compose.prod.yml up -d --build

# 清理未使用的镜像
docker image prune -f

# 完全清理（⚠️ 删除所有未使用的镜像）
docker system prune -a
```

---

# Table of Contents

- [5-Minute Quick Start](#5-minute-quick-start)
- [Development Deployment](#development-deployment)
- [Production Deployment (Single Server)](#production-deployment-single-server)
- [Production Deployment (Cloud VM)](#production-deployment-cloud-vm)
- [Client Configuration](#client-configuration)
- [Common Deployment Scenarios](#common-deployment-scenarios)
- [Post-Deployment Checklist](#post-deployment-checklist)

---

## 5-Minute Quick Start

### Prerequisites

- Docker 20.10+ and Docker Compose 2.0+ installed
- Git installed
- Linux/macOS/WSL terminal access

### Quick Commands

```bash
# 1. Clone repository (or use existing Ditto source)
git clone https://github.com/sabrogden/Ditto.git
cd Ditto

# 2. Configure environment
cp .env.example .env
JWT_SECRET=$(openssl rand -base64 32)
sed -i "s/change-me-in-production/$JWT_SECRET/" .env

# 3. Start development environment
docker-compose up -d

# 4. Access services
echo "Web panel + API: http://localhost:8080"
echo "Health Check: http://localhost:8080/health"

# 5. View logs
docker-compose logs -f
```

**That's it!** You now have Ditto Cloud running locally.

---

## Development Deployment

### Local Development (HTTP)

**Best for**: Development, testing, local access

```bash
# Start services
docker-compose up -d

# Verify deployment
curl http://localhost:8080/health

# Expected response: {"status":"ok","timestamp":"..."}

# Access web panel: http://localhost:8080
```

**Ports Used:**
- Web panel + API: 8080 (single port, Go serves both)

**Stop Services:**
```bash
docker-compose down
```

**Reset Data (⚠️ Deletes all data):**
```bash
docker-compose down -v
```

---

## Production Deployment (Single Server)

### Ubuntu/Debian Server with HTTPS

**Best for**: Production, remote access, multiple users

#### Step 1: Prepare Server

```bash
# Update system
sudo apt update && sudo apt upgrade -y

# Install Docker
curl -fsSL https://get.docker.com -o get-docker.sh
sudo sh get-docker.sh
sudo usermod -aG docker $USER
newgrp docker

# Install Docker Compose (usually included with Docker)
docker compose version
```

#### Step 2: Clone and Configure

```bash
cd /opt
sudo git clone https://github.com/sabrogden/Ditto.git
cd Ditto

# Configure environment
cp .env.example .env
JWT_SECRET=$(openssl rand -base64 32)
sed -i "s/change-me-in-production/$JWT_SECRET/" .env

# Verify configuration
cat .env | grep JWT_SECRET
```

#### Step 3: Generate SSL Certificates

**Option A: Self-Signed (Testing Only)**

```bash
chmod +x generate-certs.sh
./generate-certs.sh "ditto.local"
```

**Option B: Let's Encrypt (Production)**

```bash
# Install Certbot
sudo apt install -y certbot

# Stop any running service on port 80
sudo systemctl stop nginx 2>/dev/null || true

# Obtain certificate
sudo certbot certonly --standalone -d ditto.your-domain.com

# Copy certificates
mkdir -p certs
sudo cp /etc/letsencrypt/live/ditto.your-domain.com/fullchain.pem certs/server.crt
sudo cp /etc/letsencrypt/live/ditto.your-domain.com/privkey.pem certs/server.key
sudo chmod 644 certs/server.crt
sudo chmod 600 certs/server.key
```

#### Step 4: Deploy with HTTPS

```bash
# Start production environment
docker-compose -f docker-compose.prod.yml up -d

# Verify deployment
docker-compose -f docker-compose.prod.yml ps

# Test HTTPS endpoint
curl -k https://localhost/health
```

#### Step 5: Configure Firewall

```bash
# Enable UFW
sudo ufw enable

# Allow necessary ports
sudo ufw allow 22/tcp   # SSH
sudo ufw allow 80/tcp   # HTTP (for certbot renewals)
sudo ufw allow 443/tcp  # HTTPS

# Verify
sudo ufw status
```

#### Step 6: Set Up Auto-Restart

```bash
# Create systemd service
sudo tee /etc/systemd/system/ditto-cloud.service > /dev/null <<EOF
[Unit]
Description=Ditto Cloud Server
Requires=docker.service
After=docker.service

[Service]
Type=oneshot
RemainAfterExit=yes
WorkingDirectory=/opt/Ditto
ExecStart=/usr/bin/docker compose -f docker-compose.prod.yml up -d
ExecStop=/usr/bin/docker compose -f docker-compose.prod.yml down
TimeoutStartSec=0

[Install]
WantedBy=multi-user.target
EOF

# Enable and start
sudo systemctl enable ditto-cloud.service
sudo systemctl start ditto-cloud.service

# Verify
sudo systemctl status ditto-cloud.service
```

#### Step 7: Set Up Certificate Auto-Renewal

```bash
# Create renewal hook
sudo tee /etc/letsencrypt/renewal-hooks/deploy/ditto-restart.sh > /dev/null <<EOF
#!/bin/bash
cp /etc/letsencrypt/live/ditto.your-domain.com/fullchain.pem /opt/Ditto/certs/server.crt
cp /etc/letsencrypt/live/ditto.your-domain.com/privkey.pem /opt/Ditto/certs/server.key
chmod 644 /opt/Ditto/certs/server.crt
chmod 600 /opt/Ditto/certs/server.key
docker restart ditto-backend
EOF

sudo chmod +x /etc/letsencrypt/renewal-hooks/deploy/ditto-restart.sh

# Test renewal
sudo certbot renew --dry-run
```

---

## Production Deployment (Cloud VM)

### AWS EC2

```bash
# 1. Launch Ubuntu 22.04 EC2 instance (t3.small or larger)
# 2. Configure security groups:
#    - SSH (22): Your IP
#    - HTTP (80): 0.0.0.0/0
#    - HTTPS (443): 0.0.0.0/0

# 3. SSH into instance
ssh -i your-key.pem ubuntu@<public-ip>

# 4. Deploy (same as single server steps)
curl -fsSL https://get.docker.com -o get-docker.sh
sudo sh get-docker.sh
sudo usermod -aG docker $USER

git clone https://github.com/sabrogten/Ditto.git
cd Ditto
cp .env.example .env
JWT_SECRET=$(openssl rand -base64 32)
sed -i "s/change-me-in-production/$JWT_SECRET/" .env

# 5. Get Let's Encrypt certificate
sudo apt install -y certbot
sudo certbot certonly --standalone -d ditto.your-domain.com

mkdir -p certs
sudo cp /etc/letsencrypt/live/ditto.your-domain.com/fullchain.pem certs/server.crt
sudo cp /etc/letsencrypt/live/ditto.your-domain.com/privkey.pem certs/server.key

# 6. Start services
docker-compose -f docker-compose.prod.yml up -d
```

### DigitalOcean Droplet

```bash
# 1. Create Ubuntu 22.04 Droplet ($12/month tier recommended)
# 2. Add your SSH key during creation
# 3. Add domain in DigitalOcean networking panel
# 4. SSH into droplet
ssh root@<droplet-ip>

# 5. Deploy (same steps as above)
curl -fsSL https://get.docker.com -o get-docker.sh
sudo sh get-docker.sh

git clone https://github.com/sabrogden/Ditto.git
cd Ditto
# ... continue with configuration
```

### Google Cloud Platform (GCP)

```bash
# 1. Create Compute Engine instance (e2-small)
# 2. Allow HTTP and HTTPS traffic in firewall rules
# 3. SSH via gcloud or console
gcloud compute ssh ditto-server --zone=us-central1-a

# 4. Deploy (same steps as above)
```

### Azure VM

```bash
# 1. Create Ubuntu VM (Standard_B2s recommended)
# 2. Configure NSG rules for ports 22, 80, 443
# 3. SSH into VM
ssh azureuser@<public-ip>

# 4. Deploy (same steps as above)
```

---

## Client Configuration

### Ditto Windows Client Configuration

After deploying the cloud server, configure your Ditto desktop client:

#### Method 1: Via Settings UI

1. Open Ditto
2. Right-click system tray icon → Options
3. Navigate to "Cloud Sync" tab
4. Enter server URL: `https://ditto.your-domain.com`
5. Click "Login" and enter credentials
6. Enable "Sync on startup"

#### Method 2: Via Registry

```
Windows Registry Editor Version 5.00

[HKEY_CURRENT_USER\Software\Ditto\CloudSync]
"ServerUrl"="https://ditto.your-domain.com"
"DeviceToken"="<token-from-web-panel>"
"SyncEnabled"=dword:00000001
"SyncInterval"=dword:0000001e
"AutoDeleteDays"=dword:0000005a
```

#### Method 3: Via INI File (Portable Version)

Edit `Ditto.ini`:

```ini
[CloudSync]
ServerUrl=https://ditto.your-domain.com
DeviceToken=<token-from-web-panel>
SyncEnabled=1
SyncInterval=30
AutoDeleteDays=90
```

### Mobile/Other Clients

For any API clients, use the following configuration:

```
Base URL: https://ditto.your-domain.com
API Version: /api/v1
WebSocket: wss://ditto.your-domain.com/api/v1/ws

Authentication:
  Header: Authorization: Bearer <JWT_TOKEN>
  Or query parameter: ?token=<JWT_TOKEN> (for WebSocket)
```

---

## Common Deployment Scenarios

### Scenario 1: Personal Use (Single User, Single Device)

```bash
# Minimal deployment on a small VPS ($5-10/month)
# - SQLite database
# - Self-signed or Let's Encrypt certificate
# - Single backend instance

docker-compose -f docker-compose.prod.yml up -d
```

**Recommended Infrastructure:**
- DigitalOcean Droplet: $6/month (1 GB RAM, 1 CPU)
- Domain: ~$10/year
- **Total: ~$11/month**

### Scenario 2: Small Team (5-20 Users)

```bash
# Add PostgreSQL for better concurrency
docker-compose -f docker-compose.prod.yml \
  -f docker-compose.postgres.yml \
  up -d
```

**Recommended Infrastructure:**
- DigitalOcean Droplet: $18/month (2 GB RAM, 2 CPU)
- Domain: ~$10/year
- **Total: ~$19/month**

### Scenario 3: Enterprise (100+ Users)

```bash
# Full stack with load balancing, PostgreSQL, monitoring
docker-compose -f docker-compose.prod.yml \
  -f docker-compose.postgres.yml \
  -f docker-compose.monitoring.yml \
  up -d
```

**Recommended Infrastructure:**
- AWS: 2x t3.medium ($30/month each) + RDS PostgreSQL ($50/month)
- Application Load Balancer ($20/month)
- **Total: ~$130/month**

### Scenario 4: Air-Gapped/On-Premises

```bash
# Offline deployment without internet access
# 1. Download image on internet-connected machine
docker save ditto-backend:latest > backend.tar

# 2. Transfer to air-gapped network
scp backend.tar user@air-gapped-server:/opt/ditto/

# 3. Load image
docker load < backend.tar

# 4. Deploy
docker-compose -f docker-compose.prod.yml up -d
```

### Scenario 5: CI/CD Auto-Deployment

Create `.github/workflows/deploy.yml`:

```yaml
name: Deploy to Production

on:
  push:
    branches: [main]

jobs:
  deploy:
    runs-on: ubuntu-latest
    steps:
    - uses: actions/checkout@v3

    - name: Deploy to server
      uses: appleboy/ssh-action@v1.0.0
      with:
        host: ${{ secrets.SERVER_HOST }}
        username: ${{ secrets.SERVER_USER }}
        key: ${{ secrets.SERVER_SSH_KEY }}
        script: |
          cd /opt/Ditto
          git pull
          docker-compose -f docker-compose.prod.yml up -d --build
          docker image prune -f
```

---

## Post-Deployment Checklist

### ✅ Infrastructure

- [ ] Server OS updated
- [ ] Docker and Docker Compose installed
- [ ] Firewall configured (ports 22, 80, 443)
- [ ] SSL certificate installed and valid
- [ ] Auto-restart service configured
- [ ] Backup strategy implemented

### ✅ Application

- [ ] Environment variables configured (`.env` file)
- [ ] JWT secret is unique and secure
- [ ] Admin account initialized (env vars / CLI / first-user registration)
- [ ] Services running (`docker-compose ps`)
- [ ] Health check passing (`curl https://localhost/health`)
- [ ] Logs accessible and clean (`docker-compose logs`)
- [ ] Web panel accessible via HTTPS

### ✅ Security

- [ ] Default passwords changed
- [ ] SSH key-based authentication enabled
- [ ] Fail2Ban installed and configured
- [ ] Automatic security updates enabled
- [ ] Rate limiting configured
- [ ] CORS policy set (if applicable)

### ✅ Monitoring

- [ ] Health checks configured
- [ ] Log aggregation set up (optional)
- [ ] Alerts configured (email/Slack)
- [ ] Monitoring dashboard accessible (optional)
- [ ] Backup verification tested

### ✅ Client Setup

- [ ] Ditto clients configured with server URL
- [ ] User accounts created
- [ ] Device tokens generated
- [ ] Sync tested between devices
- [ ] End-to-end encryption enabled

### ✅ Documentation

- [ ] Server access credentials documented
- [ ] Deployment steps documented
- [ ] Backup/restore procedures tested
- [ ] Emergency contacts listed
- [ ] Runbook created

---

## Verification Commands

After deployment, run these commands to verify everything is working:

```bash
# 1. Check Docker status
docker info

# 2. Check services running
docker-compose -f docker-compose.prod.yml ps

# 3. Check backend health
curl -k https://localhost/health

# 4. Check web panel
curl -k https://localhost/

# 5. Check logs for errors
docker-compose logs backend | grep -i error

# 6. Check database
docker exec ditto-backend ls -la /app/data/

# 7. Check SSL certificate
openssl x509 -in certs/server.crt -noout -dates

# 8. Test WebSocket connection
wscat -c wss://localhost/api/v1/ws

# 9. Check resource usage
docker stats --no-stream

# 10. Verify backup procedure
docker exec ditto-backend sqlite3 /app/data/ditto_cloud.db ".backup /tmp/test-backup.db"
docker cp ditto-backend:/tmp/test-backup.db ./test-backup.db
ls -lh test-backup.db
```

---

## Useful Commands Reference

```bash
# Start services
docker-compose -f docker-compose.prod.yml up -d

# Stop services
docker-compose -f docker-compose.prod.yml down

# Restart specific service
docker-compose -f docker-compose.prod.yml restart backend

# View logs
docker-compose logs -f backend

# View resource usage
docker stats

# Access backend shell
docker exec -it ditto-backend sh

# Access database
docker exec -it ditto-backend sqlite3 /app/data/ditto_cloud.db

# Backup database
docker exec ditto-backend sqlite3 /app/data/ditto_cloud.db ".backup /tmp/backup.db"
docker cp ditto-backend:/tmp/backup.db ./backup-$(date +%Y%m%d).db

# Update services
cd /opt/Ditto
git pull
docker-compose -f docker-compose.prod.yml up -d --build

# Clean up unused images
docker image prune -f

# Full cleanup (⚠️ removes all unused images)
docker system prune -a
```

---

## Getting Help

If you encounter issues during deployment:

1. **Check Logs**: `docker-compose logs -f`
2. **Verify Configuration**: `cat .env`
3. **Check Ports**: `sudo lsof -i :80 -i :443 -i :8080`
4. **Test Connectivity**: `curl -k https://localhost/health`
5. **Review Documentation**:
   - [Full Deployment Guide](docs/DEPLOYMENT.md)
   - [Cloud Deployment Guide](docs/CLOUD_DEPLOYMENT.md)
   - [Project Plan](docs/PROJECT_PLAN.md)
6. **GitHub Issues**: https://github.com/sabrogden/Ditto/issues
7. **Community Forum**: https://github.com/sabrogden/Ditto/discussions

---

## Next Steps

After successful deployment:

1. 📱 **Configure Ditto clients** to sync with your server
2. 👥 **Create user accounts** for team members
3. 🔔 **Set up monitoring** and alerts
4. 💾 **Test backup and restore** procedures
5. 📊 **Review performance** metrics
6. 🔒 **Audit security** configuration
7. 📝 **Document customizations** for future reference

Happy syncing! 🎉
