# Cloud Server Deployment Guide | 云端服务器部署指南

**[English](#table-of-contents)** | **[中文](#目录中文版)**

> 最后审阅日期：2026-07-24（内容基于当前 Docker Compose 部署架构）

---

# 目录中文版

本指南提供 Ditto 云端服务器的全面部署说明，适用于从单服务器设置到企业级云部署的各种环境。

## 系统要求

| 组件 | 最低配置 | 推荐配置 |
|------|----------|----------|
| CPU | 2 核 | 4+ 核 |
| 内存 | 2 GB | 4+ GB |
| 存储 | 10 GB | 50+ GB（建议使用 SSD） |
| 网络 | 100 Mbps | 1 Gbps+ |
| 操作系统 | Linux (Ubuntu 20.04+), Windows Server 2019+, macOS 11+ | Linux (Ubuntu 22.04 LTS) |

## 软件依赖

- **Docker** 20.10+（容器化部署）
- **Docker Compose** 2.0+（多容器设置）
- **Go** 1.21+（从源代码构建）
- **Node.js** 18+（从源代码构建前端）
- **Nginx** 1.20+（反向代理，如不使用 Docker）
- **PostgreSQL** 15+（可选，多用户场景）
- **Certbot**（Let's Encrypt SSL 证书）

## 端口要求

| 端口 | 协议 | 用途 | 必需 |
|------|------|------|------|
| 80 | TCP | HTTP（重定向到 HTTPS） | 否 |
| 443 | TCP | HTTPS（Web 面板和 API） | 是 |
| 8080 | TCP | 后端 API（内部） | 否 |
| 8443 | TCP | 带 TLS 的后端 API | 是（如不使用反向代理） |

## 部署选项

| 选项 | 适用场景 | 复杂度 | 可扩展性 |
|------|----------|------------|-------------|
| **单服务器（裸金属）** | 个人使用、小团队 | 低 | 低 |
| **Docker Compose** | 开发、小型生产环境 | 低 | 低-中 |
| **云 VM（AWS/Azure/GCP）** | 生产环境、远程访问 | 中 | 中 |
| **Kubernetes** | 企业、高可用性 | 高 | 高 |

## Ubuntu/Debian Linux 单服务器部署

### 步骤 1: 系统更新

```bash
sudo apt update && sudo apt upgrade -y
sudo apt install -y curl wget git
```

### 步骤 2: 安装 Docker

```bash
# 安装 Docker
curl -fsSL https://get.docker.com -o get-docker.sh
sudo sh get-docker.sh

# 将用户添加到 docker 组
sudo usermod -aG docker $USER
newgrp docker

# 安装 Docker Compose
sudo apt install -y docker-compose-plugin
```

### 步骤 3: 克隆仓库

```bash
cd /opt
sudo git clone https://github.com/sabrogden/Ditto.git
cd Ditto
```

### 步骤 4: 配置环境

```bash
# 复制示例环境文件
cp .env.example .env

# 生成 JWT 密钥
JWT_SECRET=$(openssl rand -base64 32)
sed -i "s/change-me-in-production/$JWT_SECRET/" .env

# 查看配置
cat .env
```

### 步骤 5: 生成 SSL 证书

```bash
# 生成自签名证书（仅测试用）
chmod +x generate-certs.sh
./generate-certs.sh "ditto.local"

# 生产环境请使用 Let's Encrypt（见下方 SSL/TLS 章节）
```

### 步骤 6: 启动服务

```bash
# 使用 HTTPS 的生产环境部署
sudo docker-compose -f docker-compose.prod.yml up -d

# 检查状态
sudo docker-compose -f docker-compose.prod.yml ps

# 查看日志
sudo docker-compose -f docker-compose.prod.yml logs -f
```

### 步骤 7: 配置防火墙

```bash
# 启用 UFW 防火墙
sudo ufw enable

# 允许 SSH、HTTP、HTTPS
sudo ufw allow 22/tcp
sudo ufw allow 80/tcp
sudo ufw allow 443/tcp

# 验证
sudo ufw status
```

### 步骤 8: 访问 Web 管理面板

打开浏览器并访问：
- **URL**: `https://<您的服务器IP>` 或 `https://<您的域名>`
- **默认凭据**: 首次访问时创建账号

## 云服务商部署

### AWS EC2 部署

1. **启动 EC2 实例**:
   - AMI: Ubuntu Server 22.04 LTS
   - 实例类型: t3.small（2 vCPU, 2 GB RAM）或生产环境使用 t3.medium
   - 存储: 20 GB gp3
   - 安全组: 开放 SSH(22)、HTTP(80)、HTTPS(443)

2. **SSH 连接**:
```bash
ssh -i your-key.pem ubuntu@<public-ip>
```

3. **部署应用**（与单服务器步骤相同）

4. **AWS 成本估算**:
   - t3.small EC2: ~$15/月
   - 20 GB gp3 存储: ~$1.60/月
   - 数据传输 (1 GB/天): ~$10/月
   - **总计: ~$27/月**

### DigitalOcean Droplet 部署

1. **创建 Droplet**:
   - 镜像: Ubuntu 22.04 LTS x64
   - 配置: Basic (Regular) - $12/月（2 GB RAM, 1 vCPU, 50 GB SSD）

2. **配置 DNS**: 在 DigitalOcean 网络面板中添加域名

3. **部署应用**（与上述步骤相同）

4. **DigitalOcean 成本估算**:
   - 基础 Droplet（2 GB）: ~$12/月
   - **总计: ~$12/月**

## 数据库配置

### SQLite（默认）

SQLite 适用于单用户或小团队部署：

**优势:**
- 零配置
- 单文件数据库
- 无需单独的服务器进程
- 非常适合 < 10 个并发用户

**配置:**

```bash
# 在 .env 文件中
DATABASE_PATH=/app/data/ditto_cloud.db

# 启用 WAL 模式以获得更好的并发性
docker exec ditto-backend sqlite3 /app/data/ditto_cloud.db "PRAGMA journal_mode=WAL;"
```

### PostgreSQL（多用户）

对于较大的并发用户部署：

**优势:**
- 更好的并发性
- 水平扩展支持
- 高级备份/复制功能
- 支持数千用户

## 安全加固

### 防火墙配置

```bash
# UFW（Ubuntu/Debian）
sudo ufw default deny incoming
sudo ufw default allow outgoing
sudo ufw allow 22/tcp   # SSH
sudo ufw allow 80/tcp   # HTTP
sudo ufw allow 443/tcp  # HTTPS
sudo ufw enable
```

### Docker 安全最佳实践

```yaml
# 在 docker-compose.yml 中
services:
  backend:
    # 以非 root 用户运行
    user: "1000:1000"
    
    # 只读根文件系统
    read_only: true
    tmpfs:
      - /tmp
    
    # 删除不必要的权限
    cap_drop:
      - ALL
    cap_add:
      - NET_BIND_SERVICE
    
    # 资源限制
    deploy:
      resources:
        limits:
          memory: 512M
          cpus: '0.5'
```

## 备份与灾难恢复

### 自动备份脚本

创建 `/opt/ditto-backup/backup.sh`:

```bash
#!/bin/bash
set -e

BACKUP_DIR="/opt/ditto-backups"
DATE=$(date +%Y%m%d-%H%M%S)
RETENTION_DAYS=30

# 创建备份目录
mkdir -p $BACKUP_DIR

# 备份数据库
docker exec ditto-backend sqlite3 /app/data/ditto_cloud.db ".backup /tmp/backup.db"
docker cp ditto-backend:/tmp/backup.db $BACKUP_DIR/ditto-db-$DATE.db

# 压缩数据库备份
gzip $BACKUP_DIR/ditto-db-$DATE.db

# 删除旧备份
find $BACKUP_DIR -type f -mtime +$RETENTION_DAYS -delete

echo "备份完成: $DATE"
```

设置为可执行并添加到 cron:

```bash
chmod +x /opt/ditto-backup/backup.sh

# 添加到 crontab（每天凌晨 2 点）
(crontab -l 2>/dev/null; echo "0 2 * * * /opt/ditto-backup/backup.sh") | crontab -
```

## 常见问题排查

### 后端无法启动

```bash
# 检查日志
docker logs ditto-backend

# 常见修复:

# 1. 数据库文件权限
docker exec ditto-backend chown -R 1000:1000 /app/data

# 2. 端口已被占用
sudo lsof -i :8080
sudo fuser -k 8080/tcp
```

### WebSocket 连接失败

```bash
# 验证 nginx 配置包含 WebSocket 头部
grep -A 5 "proxy_set_header Upgrade" /etc/nginx/nginx.conf

# 检查后端日志中的 WebSocket 错误
docker logs ditto-backend 2>&1 | grep -i websocket
```

### SSL 证书错误

```bash
# 检查证书有效期
openssl x509 -in certs/server.crt -noout -dates

# 验证证书匹配域名
openssl x509 -in certs/server.crt -noout -text | grep "Subject:"

# 重新生成证书
./generate-certs.sh "your-domain.com"
docker-compose -f docker-compose.prod.yml restart
```

---

# Table of Contents

- [Prerequisites](#prerequisites)
- [Deployment Options](#deployment-options)
- [Single Server Deployment](#single-server-deployment)
  - [Ubuntu/Debian Linux](#ubuntudebian-linux)
  - [CentOS/RHEL Linux](#centosrhel-linux)
  - [Windows Server](#windows-server)
- [Docker Deployment](#docker-deployment)
  - [Docker Compose (Recommended)](#docker-compose-recommended)
  - [Standalone Docker](#standalone-docker)
- [Cloud Provider Deployment](#cloud-provider-deployment)
  - [AWS EC2 Deployment](#aws-ec2-deployment)
  - [Azure VM Deployment](#azure-vm-deployment)
  - [Google Cloud Platform (GCP) Deployment](#google-cloud-platform-gcp-deployment)
  - [DigitalOcean Droplet Deployment](#digitalocean-droplet-deployment)
- [Kubernetes Deployment](#kubernetes-deployment)
  - [Helm Chart Deployment](#helm-chart-deployment)
  - [Manual Kubernetes Configuration](#manual-kubernetes-configuration)
- [Database Configuration](#database-configuration)
  - [SQLite (Default)](#sqlite-default)
  - [PostgreSQL (Multi-User)](#postgresql-multi-user)
- [Load Balancing & High Availability](#load-balancing--high-availability)
- [Monitoring & Observability](#monitoring--observability)
- [Security Hardening](#security-hardening)
- [Performance Tuning](#performance-tuning)
- [Backup & Disaster Recovery](#backup--disaster-recovery)
- [Troubleshooting](#troubleshooting)

---

## Prerequisites

### System Requirements

| Component | Minimum | Recommended |
|-----------|---------|-------------|
| CPU | 2 cores | 4+ cores |
| RAM | 2 GB | 4+ GB |
| Storage | 10 GB | 50+ GB (SSD preferred) |
| Network | 100 Mbps | 1 Gbps+ |
| OS | Linux (Ubuntu 20.04+), Windows Server 2019+, macOS 11+ | Linux (Ubuntu 22.04 LTS) |

### Software Dependencies

- **Docker** 20.10+ (for containerized deployment)
- **Docker Compose** 2.0+ (for multi-container setup)
- **Go** 1.21+ (for building from source)
- **Node.js** 18+ (for building frontend from source)
- **Nginx** 1.20+ (for reverse proxy, if not using Docker)
- **PostgreSQL** 15+ (optional, for multi-user scenarios)
- **Certbot** (for Let's Encrypt SSL certificates)

### Network Requirements

| Port | Protocol | Purpose | Required |
|------|----------|---------|----------|
| 80 | TCP | HTTP (redirect to HTTPS) | No |
| 443 | TCP | HTTPS (web panel & API) | Yes |
| 8080 | TCP | Backend API (internal) | No |
| 8443 | TCP | Backend API with TLS | Yes (if not using reverse proxy) |
| 5432 | TCP | PostgreSQL (if used) | No (internal only) |

---

## Deployment Options

Choose the deployment option that best fits your needs:

| Option | Best For | Complexity | Scalability |
|--------|----------|------------|-------------|
| **Single Server (Bare Metal)** | Personal use, small teams | Low | Low |
| **Docker Compose** | Development, small production | Low | Low-Medium |
| **Cloud VM (AWS/Azure/GCP)** | Production, remote access | Medium | Medium |
| **Kubernetes** | Enterprise, high availability | High | High |
| **Serverless** | Variable workloads, cost optimization | High | Auto |

---

## Single Server Deployment

### Ubuntu/Debian Linux

#### Step 1: System Update

```bash
sudo apt update && sudo apt upgrade -y
sudo apt install -y curl wget git
```

#### Step 2: Install Docker

```bash
# Install Docker
curl -fsSL https://get.docker.com -o get-docker.sh
sudo sh get-docker.sh

# Add user to docker group
sudo usermod -aG docker $USER
newgrp docker

# Install Docker Compose
sudo apt install -y docker-compose-plugin
```

#### Step 3: Clone Repository

```bash
cd /opt
sudo git clone https://github.com/sabrogden/Ditto.git
cd Ditto
```

#### Step 4: Configure Environment

```bash
# Copy example environment file
cp .env.example .env

# Generate JWT secret
JWT_SECRET=$(openssl rand -base64 32)
sed -i "s/change-me-in-production/$JWT_SECRET/" .env

# Review configuration
cat .env
```

#### Step 5: Generate SSL Certificates

```bash
# For self-signed certificates (testing only)
chmod +x generate-certs.sh
./generate-certs.sh "ditto.local"

# For production, use Let's Encrypt (see SSL/TLS section below)
```

#### Step 6: Start Services

```bash
# Production deployment with HTTPS
sudo docker-compose -f docker-compose.prod.yml up -d

# Check status
sudo docker-compose -f docker-compose.prod.yml ps

# View logs
sudo docker-compose -f docker-compose.prod.yml logs -f
```

#### Step 7: Configure Firewall

```bash
# Enable UFW firewall
sudo ufw enable

# Allow SSH, HTTP, HTTPS
sudo ufw allow 22/tcp
sudo ufw allow 80/tcp
sudo ufw allow 443/tcp

# Verify
sudo ufw status
```

#### Step 8: Access Web Panel

Open browser and navigate to:
- **URL**: `https://<your-server-ip>` or `https://<your-domain>`
- **Default Credentials**: Create account on first visit

#### Step 9: Set Up Auto-Start

```bash
# Create systemd service
sudo tee /etc/systemd/system/ditto.service > /dev/null <<EOF
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

# Enable service
sudo systemctl enable ditto.service
sudo systemctl start ditto.service
```

### CentOS/RHEL Linux

```bash
# Install dependencies
sudo yum update -y
sudo yum install -y yum-utils curl wget git

# Install Docker
sudo yum-config-manager --add-repo https://download.docker.com/linux/centos/docker-ce.repo
sudo yum install -y docker-ce docker-ce-cli containerd.io
sudo systemctl start docker
sudo systemctl enable docker

# Install Docker Compose
sudo curl -L "https://github.com/docker/compose/releases/latest/download/docker-compose-$(uname -s)-$(uname -m)" -o /usr/local/bin/docker-compose
sudo chmod +x /usr/local/bin/docker-compose

# Continue with steps from Ubuntu section (Step 3 onwards)
```

### Windows Server

#### Step 1: Install Docker Desktop

1. Download Docker Desktop for Windows from [docker.com](https://www.docker.com/products/docker-desktop)
2. Run installer and follow prompts
3. Restart system when prompted

#### Step 2: Install Git

1. Download Git for Windows from [git-scm.com](https://git-scm.com/download/win)
2. Run installer with default options

#### Step 3: Clone and Configure

Open PowerShell as Administrator:

```powershell
cd C:\
git clone https://github.com/sabrogden/Ditto.git
cd Ditto

# Copy environment file
Copy-Item .env.example .env

# Generate JWT secret (requires OpenSSL)
# Download OpenSSL for Windows or use WSL
$JWT_SECRET = -join ((65..90) + (97..122) + (48..57) | Get-Random -Count 32 | ForEach-Object {[char]$_})
(Get-Content .env) -replace 'change-me-in-production', $JWT_SECRET | Set-Content .env
```

#### Step 4: Generate Certificates

```powershell
# Use PowerShell script to generate self-signed cert
$cert = New-SelfSignedCertificate -DnsName "ditto.local" -CertStoreLocation "cert:\LocalMachine\My"
Export-PfxCertificate -Cert $cert -FilePath certs\server.pfx -Password (ConvertTo-SecureString -String "password" -Force -AsPlainText)
```

#### Step 5: Start Services

```powershell
docker-compose -f docker-compose.prod.yml up -d
```

---

## Docker Deployment

### Docker Compose (Recommended)

See the [Single Server Deployment](#single-server-deployment) section above for complete Docker Compose instructions.

### Standalone Docker

For simple setups without Docker Compose:

```bash
# Create network
docker network create ditto-net

# Start backend
docker run -d \
  --name ditto-backend \
  --network ditto-net \
  -p 8080:8080 \
  -v ditto-data:/app/data \
  -e PORT=8080 \
  -e DATABASE_PATH=/app/data/ditto_cloud.db \
  -e JWT_SECRET=$(openssl rand -base64 32) \
  --restart unless-stopped \
  ditto-backend:latest

# Start frontend
docker run -d \
  --name ditto-frontend \
  --network ditto-net \
  -p 80:80 \
  --depends-on ditto-backend \
  --restart unless-stopped \
  ditto-frontend:latest
```

---

## Cloud Provider Deployment

### AWS EC2 Deployment

#### Step 1: Launch EC2 Instance

1. **Open AWS Console** → EC2 → Launch Instance
2. **Choose AMI**: Ubuntu Server 22.04 LTS (HVM), SSD Volume Type
3. **Instance Type**: t3.small (2 vCPU, 2 GB RAM) or t3.medium for production
4. **Configure Instance**:
   - Storage: 20 GB gp3
   - Network: Default VPC with public subnet
   - Auto-assign Public IP: Enable
5. **Security Group**:
   - Inbound rules:
     - SSH (22): Your IP only
     - HTTP (80): 0.0.0.0/0
     - HTTPS (443): 0.0.0.0/0
6. **Key Pair**: Create or use existing key pair
7. **Launch Instance**

#### Step 2: Connect to Instance

```bash
# SSH into instance
chmod 400 your-key.pem
ssh -i your-key.pem ubuntu@<public-ip>
```

#### Step 3: Install Dependencies and Deploy

```bash
# Update system
sudo apt update && sudo apt upgrade -y

# Install Docker
curl -fsSL https://get.docker.com -o get-docker.sh
sudo sh get-docker.sh
sudo usermod -aG docker $USER

# Install Docker Compose
sudo apt install -y docker-compose-plugin

# Clone repository
git clone https://github.com/sabrogden/Ditto.git
cd Ditto

# Configure
cp .env.example .env
JWT_SECRET=$(openssl rand -base64 32)
sed -i "s/change-me-in-production/$JWT_SECRET/" .env

# Generate certificates (replace with your domain)
chmod +x generate-certs.sh
./generate-certs.sh "ditto.your-domain.com"

# Start services
docker-compose -f docker-compose.prod.yml up -d
```

#### Step 4: Configure Route 53 (Optional)

If using AWS Route 53 for DNS:

1. Go to Route 53 → Hosted Zones
2. Create record set:
   - Name: `ditto.your-domain.com`
   - Type: A
   - Value: `<EC2 public IP>`
3. Wait for DNS propagation (up to 48 hours, usually minutes)

#### Step 5: Set Up Let's Encrypt

```bash
# Install Certbot
sudo apt install -y certbot

# Obtain certificate (stop nginx temporarily if running)
sudo certbot certonly --standalone -d ditto.your-domain.com

# Copy certificates
sudo cp /etc/letsencrypt/live/ditto.your-domain.com/fullchain.pem certs/server.crt
sudo cp /etc/letsencrypt/live/ditto.your-domain.com/privkey.pem certs/server.key

# Restart services
docker-compose -f docker-compose.prod.yml restart
```

#### Step 6: Set Up CloudWatch Monitoring

```bash
# Install CloudWatch agent
sudo apt install -y amazon-cloudwatch-agent

# Configure monitoring
sudo /opt/aws/amazon-cloudwatch-agent/bin/amazon-cloudwatch-agent-ctl -a fetch-config -m ec2 -c file:/opt/aws/amazon-cloudwatch-agent/bin/config.json -s
```

#### AWS Cost Estimation

| Resource | Cost (Monthly) |
|----------|----------------|
| t3.small EC2 | ~$15 |
| 20 GB gp3 Storage | ~$1.60 |
| Data Transfer (1 GB/day) | ~$10 |
| **Total** | **~$27/month** |

### Azure VM Deployment

#### Step 1: Create Virtual Machine

1. **Open Azure Portal** → Virtual Machines → Create
2. **Basics**:
   - Subscription: Your subscription
   - Resource Group: Create new (e.g., `ditto-rg`)
   - Virtual machine name: `ditto-server`
   - Region: Choose nearest
   - Image: Ubuntu Server 22.04 LTS
   - Size: Standard_B2s (2 vCPU, 4 GB RAM)
   - Authentication: SSH public key
3. **Disks**:
   - OS disk type: Premium SSD
   - Size: 30 GB
4. **Networking**:
   - Virtual network: Default
   - Public IP: Create new
   - NIC network security group: Basic
   - Inbound port rules:
     - SSH (22): Your IP
     - HTTP (80): Any
     - HTTPS (443): Any
5. **Create VM**

#### Step 2: Configure DNS

1. Go to Public IP addresses
2. Create DNS name label (e.g., `ditto-server.eastus.cloudapp.azure.com`)

Or use Azure DNS Zone for custom domain

#### Step 3: Deploy Application

SSH into the VM and follow the same steps as [AWS EC2 Deployment](#aws-ec2-deployment) (Step 3 onwards)

#### Step 4: Set Up Azure Monitor

1. Go to your VM → Monitoring → Insights
2. Enable Azure Monitor for VMs
3. Configure alerts for CPU, memory, disk usage

#### Azure Cost Estimation

| Resource | Cost (Monthly) |
|----------|----------------|
| Standard_B2s VM | ~$15 |
| 30 GB Premium SSD | ~$3.60 |
| Bandwidth (1 GB/day) | ~$10 |
| **Total** | **~$29/month** |

### Google Cloud Platform (GCP) Deployment

#### Step 1: Create Compute Engine Instance

1. **Open GCP Console** → Compute Engine → VM instances → Create
2. **Configure**:
   - Name: `ditto-server`
   - Region/Zone: Choose nearest
   - Machine type: e2-small (2 vCPU, 2 GB RAM)
   - Boot disk: Ubuntu 22.04 LTS, 20 GB balanced persistent disk
3. **Firewall**:
   - Allow HTTP traffic
   - Allow HTTPS traffic
4. **Create**

#### Step 2: Configure External IP

1. Go to VPC network → External IP addresses
2. Reserve static IP address (optional, for persistence)

#### Step 3: Set Up Cloud DNS

1. Go to Network Services → Cloud DNS
2. Create DNS zone for your domain
3. Create A record pointing to VM's external IP

#### Step 4: Deploy Application

SSH into the instance and follow the deployment steps from [AWS EC2 Deployment](#aws-ec2-deployment)

#### GCP Cost Estimation

| Resource | Cost (Monthly) |
|----------|----------------|
| e2-small VM | ~$10 |
| 20 GB Balanced PD | ~$2 |
| Network egress (30 GB) | ~$2.60 |
| **Total** | **~$15/month** |

### DigitalOcean Droplet Deployment

#### Step 1: Create Droplet

1. **Open DigitalOcean Console** → Create → Droplets
2. **Choose**:
   - Region: Nearest to your users
   - Image: Ubuntu 22.04 LTS x64
   - Size: Basic (Regular) - $12/month (2 GB RAM, 1 vCPU, 50 GB SSD)
   - Authentication: SSH key
3. **Create Droplet**

#### Step 2: Configure DNS

1. Go to Networking → Domains
2. Add your domain
3. Create A record pointing to droplet IP

#### Step 3: Deploy Application

SSH into the droplet:

```bash
ssh root@<droplet-ip>

# Follow deployment steps
apt update && apt upgrade -y
curl -fsSL https://get.docker.com -o get-docker.sh
sudo sh get-docker.sh
# ... continue with standard deployment steps
```

#### Step 4: Set Up Monitoring

DigitalOcean provides built-in monitoring:
1. Go to your Droplet → Graphs
2. Enable alerts for CPU, disk, memory

#### DigitalOcean Cost Estimation

| Resource | Cost (Monthly) |
|----------|----------------|
| Basic Droplet (2 GB) | ~$12 |
| 50 GB SSD (included) | $0 |
| Transfer (2 TB included) | $0 |
| **Total** | **~$12/month** |

---

## Kubernetes Deployment

### Helm Chart Deployment

#### Step 1: Install Helm

```bash
curl https://raw.githubusercontent.com/helm/helm/main/scripts/get-helm-3 | bash
```

#### Step 2: Add Repository (when available)

```bash
helm repo add ditto https://charts.ditto-clipboard.com
helm repo update
```

#### Step 3: Deploy with Helm

```bash
# Create namespace
kubectl create namespace ditto

# Deploy with custom values
helm install ditto ditto/ditto-cloud \
  --namespace ditto \
  --set jwtSecret=$(openssl rand -base64 32) \
  --set ingress.enabled=true \
  --set ingress.hosts[0].host=ditto.your-domain.com \
  --set postgresql.enabled=true \
  --values custom-values.yaml
```

### Manual Kubernetes Configuration

#### ditto-k8s.yaml

```yaml
apiVersion: v1
kind: Namespace
metadata:
  name: ditto

---
apiVersion: v1
kind: ConfigMap
metadata:
  name: ditto-config
  namespace: ditto
data:
  PORT: "8080"
  DATABASE_PATH: "/app/data/ditto_cloud.db"
  LOG_LEVEL: "info"

---
apiVersion: v1
kind: Secret
metadata:
  name: ditto-secrets
  namespace: ditto
type: Opaque
stringData:
  jwt-secret: "your-jwt-secret-here"

---
apiVersion: v1
kind: PersistentVolumeClaim
metadata:
  name: ditto-data-pvc
  namespace: ditto
spec:
  accessModes:
    - ReadWriteOnce
  resources:
    requests:
      storage: 10Gi

---
apiVersion: apps/v1
kind: Deployment
metadata:
  name: ditto-backend
  namespace: ditto
spec:
  replicas: 1
  selector:
    matchLabels:
      app: ditto-backend
  template:
    metadata:
      labels:
        app: ditto-backend
    spec:
      containers:
      - name: backend
        image: ditto-cloud-backend:latest
        ports:
        - containerPort: 8080
        envFrom:
        - configMapRef:
            name: ditto-config
        - secretRef:
            name: ditto-secrets
        volumeMounts:
        - name: data
          mountPath: /app/data
        resources:
          requests:
            memory: "256Mi"
            cpu: "250m"
          limits:
            memory: "512Mi"
            cpu: "500m"
        livenessProbe:
          httpGet:
            path: /health
            port: 8080
          initialDelaySeconds: 10
          periodSeconds: 30
        readinessProbe:
          httpGet:
            path: /health
            port: 8080
          initialDelaySeconds: 5
          periodSeconds: 10
      volumes:
      - name: data
        persistentVolumeClaim:
          claimName: ditto-data-pvc

---
apiVersion: apps/v1
kind: Deployment
metadata:
  name: ditto-frontend
  namespace: ditto
spec:
  replicas: 1
  selector:
    matchLabels:
      app: ditto-frontend
  template:
    metadata:
      labels:
        app: ditto-frontend
    spec:
      containers:
      - name: frontend
        image: ditto-cloud-frontend:latest
        ports:
        - containerPort: 80
        resources:
          requests:
            memory: "128Mi"
            cpu: "100m"
          limits:
            memory: "256Mi"
            cpu: "250m"
        livenessProbe:
          httpGet:
            path: /
            port: 80
          initialDelaySeconds: 5
          periodSeconds: 30

---
apiVersion: v1
kind: Service
metadata:
  name: ditto-backend-service
  namespace: ditto
spec:
  selector:
    app: ditto-backend
  ports:
  - protocol: TCP
    port: 8080
    targetPort: 8080
  type: ClusterIP

---
apiVersion: v1
kind: Service
metadata:
  name: ditto-frontend-service
  namespace: ditto
spec:
  selector:
    app: ditto-frontend
  ports:
  - protocol: TCP
    port: 80
    targetPort: 80
  type: ClusterIP

---
apiVersion: networking.k8s.io/v1
kind: Ingress
metadata:
  name: ditto-ingress
  namespace: ditto
  annotations:
    kubernetes.io/ingress.class: nginx
    cert-manager.io/cluster-issuer: letsencrypt-prod
    nginx.ingress.kubernetes.io/proxy-read-timeout: "86400"
    nginx.ingress.kubernetes.io/proxy-send-timeout: "86400"
spec:
  tls:
  - hosts:
    - ditto.your-domain.com
    secretName: ditto-tls-secret
  rules:
  - host: ditto.your-domain.com
    http:
      paths:
      - path: /api
        pathType: Prefix
        backend:
          service:
            name: ditto-backend-service
            port:
              number: 8080
      - path: /ws
        pathType: Prefix
        backend:
          service:
            name: ditto-backend-service
            port:
              number: 8080
      - path: /
        pathType: Prefix
        backend:
          service:
            name: ditto-frontend-service
            port:
              number: 80
```

#### Deploy to Kubernetes

```bash
# Apply configuration
kubectl apply -f ditto-k8s.yaml

# Check deployment status
kubectl get all -n ditto

# View logs
kubectl logs -f deployment/ditto-backend -n ditto
kubectl logs -f deployment/ditto-frontend -n ditto

# Set up cert-manager for SSL (if not already installed)
kubectl apply -f https://github.com/cert-manager/cert-manager/releases/download/v1.13.0/cert-manager.yaml

# Create ClusterIssuer for Let's Encrypt
cat <<EOF | kubectl apply -f -
apiVersion: cert-manager.io/v1
kind: ClusterIssuer
metadata:
  name: letsencrypt-prod
spec:
  acme:
    server: https://acme-v02.api.letsencrypt.org/directory
    email: your-email@domain.com
    privateKeySecretRef:
      name: letsencrypt-prod-key
    solvers:
    - http01:
        ingress:
          class: nginx
EOF
```

---

## Database Configuration

### SQLite (Default)

SQLite is suitable for single-user or small-team deployments:

**Advantages:**
- Zero configuration
- Single file database
- No separate server process
- Perfect for < 10 concurrent users

**Configuration:**

```bash
# In .env file
DATABASE_PATH=/app/data/ditto_cloud.db

# Enable WAL mode for better concurrency
docker exec ditto-backend sqlite3 /app/data/ditto_cloud.db "PRAGMA journal_mode=WAL;"
```

**Performance Tuning:**

```sql
-- Execute inside backend container
docker exec -it ditto-backend sh

sqlite3 /app/data/ditto_cloud.db <<EOF
PRAGMA journal_mode=WAL;
PRAGMA synchronous=NORMAL;
PRAGMA cache_size=-64000;  -- 64MB cache
PRAGMA temp_store=MEMORY;
PRAGMA mmap_size=268435456;  -- 256MB memory mapping
EOF
```

### PostgreSQL (Multi-User)

For larger deployments with many concurrent users:

**Advantages:**
- Better concurrency
- Horizontal scaling support
- Advanced backup/replication
- Supports thousands of users

#### Step 1: Add PostgreSQL to Docker Compose

Create `docker-compose.postgres.yml`:

```yaml
version: '3.8'

services:
  backend:
    environment:
      - DATABASE_URL=postgresql://ditto:password@postgres:5432/ditto_db
    depends_on:
      postgres:
        condition: service_healthy

  postgres:
    image: postgres:15-alpine
    container_name: ditto-postgres
    restart: unless-stopped
    environment:
      POSTGRES_DB: ditto_db
      POSTGRES_USER: ditto
      POSTGRES_PASSWORD: password
    ports:
      - "5432:5432"
    volumes:
      - postgres-data:/var/lib/postgresql/data
    healthcheck:
      test: ["CMD-SHELL", "pg_isready -U ditto"]
      interval: 10s
      timeout: 5s
      retries: 5

volumes:
  postgres-data:
```

#### Step 2: Deploy with PostgreSQL

```bash
docker-compose -f docker-compose.prod.yml -f docker-compose.postgres.yml up -d
```

#### Step 3: Migrate from SQLite to PostgreSQL (Optional)

```bash
# Export SQLite data
docker exec ditto-backend sqlite3 /app/data/ditto_cloud.db ".dump" > dump.sql

# Import to PostgreSQL
docker exec -i ditto-postgres psql -U ditto -d ditto_db < dump.sql

# Update backend configuration to use PostgreSQL
# Restart backend service
docker-compose -f docker-compose.prod.yml -f docker-compose.postgres.yml restart backend
```

---

## Load Balancing & High Availability

For enterprise deployments with high availability requirements:

### Nginx Load Balancer

```nginx
upstream ditto-backend {
    least_conn;
    server backend1:8080 max_fails=3 fail_timeout=30s;
    server backend2:8080 max_fails=3 fail_timeout=30s;
    server backend3:8080 max_fails=3 fail_timeout=30s;
}

server {
    listen 443 ssl;
    server_name ditto.your-domain.com;

    ssl_certificate /etc/letsencrypt/live/ditto.your-domain.com/fullchain.pem;
    ssl_certificate_key /etc/letsencrypt/live/ditto.your-domain.com/privkey.pem;

    location /api {
        proxy_pass http://ditto-backend;
        proxy_http_version 1.1;
        proxy_set_header Upgrade $http_upgrade;
        proxy_set_header Connection "upgrade";
        proxy_set_header Host $host;
        proxy_set_header X-Real-IP $remote_addr;
        proxy_set_header X-Forwarded-For $proxy_add_x_forwarded_for;
        proxy_set_header X-Forwarded-Proto $scheme;
        proxy_read_timeout 86400s;
        proxy_send_timeout 86400s;
    }

    location /ws {
        proxy_pass http://ditto-backend;
        proxy_http_version 1.1;
        proxy_set_header Upgrade $http_upgrade;
        proxy_set_header Connection "upgrade";
        proxy_set_header Host $host;
        proxy_read_timeout 86400s;
    }

    location / {
        proxy_pass http://ditto-frontend;
        proxy_set_header Host $host;
        proxy_set_header X-Real-IP $remote_addr;
    }
}
```

### PostgreSQL Replication

For database high availability:

```yaml
# Use PostgreSQL with streaming replication
services:
  postgres-master:
    image: bitnami/postgresql:15
    environment:
      POSTGRESQL_REPLICATION_MODE: master
      POSTGRESQL_REPLICATION_USER: repl_user
      POSTGRESQL_REPLICATION_PASSWORD: repl_password
      # ... other configuration

  postgres-slave:
    image: bitnami/postgresql:15
    environment:
      POSTGRESQL_REPLICATION_MODE: slave
      POSTGRESQL_MASTER_HOST: postgres-master
      POSTGRESQL_MASTER_PORT_NUMBER: 5432
      # ... other configuration
```

---

## Monitoring & Observability

### Prometheus + Grafana Stack

#### Step 1: Add Monitoring Services

Add to `docker-compose.yml`:

```yaml
  prometheus:
    image: prom/prometheus:latest
    container_name: ditto-prometheus
    restart: unless-stopped
    ports:
      - "9090:9090"
    volumes:
      - ./monitoring/prometheus.yml:/etc/prometheus/prometheus.yml
      - prometheus-data:/prometheus
    networks:
      - ditto-net

  grafana:
    image: grafana/grafana:latest
    container_name: ditto-grafana
    restart: unless-stopped
    ports:
      - "3000:3000"
    environment:
      - GF_SECURITY_ADMIN_PASSWORD=your-grafana-password
    volumes:
      - grafana-data:/var/lib/grafana
      - ./monitoring/grafana/dashboards:/etc/grafana/provisioning/dashboards
    networks:
      - ditto-net
```

#### Step 2: Configure Prometheus

Create `monitoring/prometheus.yml`:

```yaml
global:
  scrape_interval: 15s

scrape_configs:
  - job_name: 'ditto-backend'
    static_configs:
      - targets: ['backend:8080']
    metrics_path: '/metrics'
```

#### Step 3: Start Monitoring Stack

```bash
docker-compose up -d
# Access Grafana at http://localhost:3000
```

### Log Aggregation with ELK Stack

For production environments with many users:

```yaml
  elasticsearch:
    image: docker.elastic.co/elasticsearch/elasticsearch:8.10.0
    environment:
      - discovery.type=single-node
      - xpack.security.enabled=false
    volumes:
      - elasticsearch-data:/usr/share/elasticsearch/data

  logstash:
    image: docker.elastic.co/logstash/logstash:8.10.0
    volumes:
      - ./monitoring/logstash.conf:/usr/share/logstash/pipeline/logstash.conf

  kibana:
    image: docker.elastic.co/kibana/kibana:8.10.0
    ports:
      - "5601:5601"
```

---

## Security Hardening

### Firewall Configuration

```bash
# UFW (Ubuntu/Debian)
sudo ufw default deny incoming
sudo ufw default allow outgoing
sudo ufw allow 22/tcp   # SSH
sudo ufw allow 80/tcp   # HTTP
sudo ufw allow 443/tcp  # HTTPS
sudo ufw enable

# firewalld (CentOS/RHEL)
sudo firewall-cmd --permanent --add-service=ssh
sudo firewall-cmd --permanent --add-service=http
sudo firewall-cmd --permanent --add-service=https
sudo firewall-cmd --reload
```

### Fail2Ban Setup

```bash
sudo apt install -y fail2ban

# Create jail configuration
sudo tee /etc/fail2ban/jail.local > /dev/null <<EOF
[DEFAULT]
bantime = 3600
findtime = 600
maxretry = 5

[sshd]
enabled = true

[ditto-auth]
enabled = true
filter = ditto-auth
logpath = /var/lib/docker/volumes/ditto_ditto-data/_data/*.log
maxretry = 10
bantime = 86400
EOF
```

### Docker Security Best Practices

```yaml
# In docker-compose.yml
services:
  backend:
    # Run as non-root user
    user: "1000:1000"
    
    # Read-only root filesystem
    read_only: true
    tmpfs:
      - /tmp
    
    # Drop unnecessary capabilities
    cap_drop:
      - ALL
    cap_add:
      - NET_BIND_SERVICE
    
    # Resource limits
    deploy:
      resources:
        limits:
          memory: 512M
          cpus: '0.5'
        reservations:
          memory: 256M
```

### Regular Security Updates

```bash
# Enable automatic security updates (Ubuntu)
sudo apt install -y unattended-upgrades
sudo dpkg-reconfigure -plow unattended-upgrades

# Set up weekly update check
sudo systemctl enable unattended-upgrades
```

---

## Performance Tuning

### Database Optimization

```sql
-- Create indexes for common queries
CREATE INDEX IF NOT EXISTS idx_clips_user_updated ON clips(user_id, updated_at DESC);
CREATE INDEX IF NOT EXISTS idx_clips_user_crc ON clips(user_id, crc);
CREATE INDEX IF NOT EXISTS idx_sync_logs_user_time ON sync_logs(user_id, synced_at DESC);

-- Vacuum database periodically
VACUUM;
ANALYZE;
```

### Nginx Optimization

```nginx
# Add to nginx configuration
http {
    # Enable gzip compression
    gzip on;
    gzip_types text/plain application/json application/javascript text/css;
    gzip_min_length 1000;
    
    # Cache static assets
    location ~* \.(jpg|jpeg|png|gif|ico|css|js)$ {
        expires 1y;
        add_header Cache-Control "public, immutable";
    }
    
    # WebSocket support
    map $http_upgrade $connection_upgrade {
        default upgrade;
        '' close;
    }
}
```

### Backend Optimization

```bash
# In .env file
# Increase worker pool size
GO_MAX_PROCS=4

# Optimize database connection pool
DB_MAX_OPEN_CONNS=25
DB_MAX_IDLE_CONNS=10
DB_CONN_MAX_LIFETIME=300
```

---

## Backup & Disaster Recovery

### Automated Backup Script

Create `/opt/ditto-backup/backup.sh`:

```bash
#!/bin/bash
set -e

BACKUP_DIR="/opt/ditto-backups"
DATE=$(date +%Y%m%d-%H%M%S)
RETENTION_DAYS=30

# Create backup directory
mkdir -p $BACKUP_DIR

# Backup database
docker exec ditto-backend sqlite3 /app/data/ditto_cloud.db ".backup /tmp/backup.db"
docker cp ditto-backend:/tmp/backup.db $BACKUP_DIR/ditto-db-$DATE.db

# Backup certificates
tar czf $BACKUP_DIR/ditto-certs-$DATE.tar.gz -C /opt/Ditto/certs .

# Backup configuration
cp /opt/Ditto/.env $BACKUP_DIR/ditto-env-$DATE.bak

# Compress database backup
gzip $BACKUP_DIR/ditto-db-$DATE.db

# Remove old backups
find $BACKUP_DIR -type f -mtime +$RETENTION_DAYS -delete

echo "Backup completed: $DATE"
```

Make executable and add to cron:

```bash
chmod +x /opt/ditto-backup/backup.sh

# Add to crontab (daily at 2 AM)
(crontab -l 2>/dev/null; echo "0 2 * * * /opt/ditto-backup/backup.sh") | crontab -
```

### Disaster Recovery Procedure

```bash
# 1. Stop all services
docker-compose down

# 2. Restore database backup
gunzip -c /opt/ditto-backups/ditto-db-20231201.db.gz > /tmp/ditto.db
docker cp /tmp/ditto.db ditto-backend:/app/data/ditto_cloud.db

# 3. Restore certificates
tar xzf /opt/ditto-backups/ditto-certs-20231201.tar.gz -C /opt/Ditto/

# 4. Restore configuration
cp /opt/ditto-backups/ditto-env-20231201.bak /opt/Ditto/.env

# 5. Restart services
docker-compose up -d

# 6. Verify services
docker-compose ps
curl -k https://localhost/health
```

---

## Troubleshooting

### Common Issues

#### Backend Won't Start

```bash
# Check logs
docker logs ditto-backend

# Common fixes:

# 1. Database file permissions
docker exec ditto-backend chown -R 1000:1000 /app/data

# 2. Port already in use
sudo lsof -i :8080
sudo fuser -k 8080/tcp

# 3. Invalid JWT secret
cat .env | grep JWT_SECRET
```

#### WebSocket Connection Fails

```bash
# Verify nginx configuration includes WebSocket headers
grep -A 5 "proxy_set_header Upgrade" /etc/nginx/nginx.conf

# Check backend logs for WebSocket errors
docker logs ditto-backend 2>&1 | grep -i websocket
```

#### SSL Certificate Errors

```bash
# Check certificate expiry
openssl x509 -in certs/server.crt -noout -dates

# Verify certificate matches domain
openssl x509 -in certs/server.crt -noout -text | grep "Subject:"

# Regenerate certificates
./generate-certs.sh "your-domain.com"
docker-compose -f docker-compose.prod.yml restart
```

#### Database Lock Issues

```bash
# Check for database locks
docker exec ditto-backend ls -la /app/data/

# Convert to WAL mode
docker exec ditto-backend sqlite3 /app/data/ditto_cloud.db "PRAGMA journal_mode=WAL;"

# If still locked, backup and restore
docker exec ditto-backend sqlite3 /app/data/ditto_cloud.db ".backup /tmp/fixed.db"
docker exec ditto-backend mv /tmp/fixed.db /app/data/ditto_cloud.db
```

### Performance Issues

```bash
# Check container resource usage
docker stats

# Check database size
docker exec ditto-backend du -sh /app/data/

# Analyze slow queries
docker exec ditto-backend sqlite3 /app/data/ditto_cloud.db "EXPLAIN QUERY PLAN SELECT * FROM clips ORDER BY updated_at DESC LIMIT 20;"

# Vacuum database
docker exec ditto-backend sqlite3 /app/data/ditto_cloud.db "VACUUM; ANALYZE;"
```

### Log Analysis

```bash
# View recent errors
docker logs --tail 100 ditto-backend | grep -i error

# Follow specific service logs
docker-compose logs -f backend | grep -i "sync\|websocket\|database"

# Export logs for analysis
docker logs ditto-backend > backend-$(date +%Y%m%d).log 2>&1
```

---

## Next Steps

After successful deployment:

1. **Create Admin Account**: Visit web panel and register first user
2. **Configure Ditto Client**: Point desktop clients to your server URL
3. **Set Up Monitoring**: Configure alerts for critical metrics
4. **Test Backup**: Perform a test restore to verify backup integrity
5. **Review Security**: Audit firewall rules and access controls
6. **Document Configuration**: Record all customizations for future reference

For additional support:
- **GitHub Issues**: https://github.com/sabrogden/Ditto/issues
- **Wiki**: https://github.com/sabrogden/Ditto/wiki
- **Community Forum**: https://github.com/sabrogden/Ditto/discussions
