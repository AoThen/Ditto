# Docker Deployment Guide | Docker 部署指南

**[English](#table-of-contents)** | **[中文](#目录中文版)**

---

# 目录中文版

本指南介绍如何使用 Docker 部署带云端同步和 Web 管理面板的 Ditto。

## 架构概览

```
┌─────────────────┐         ┌──────────────────┐         ┌─────────────────┐
│  Ditto 客户端    │  HTTPS  │   Go 云端服务     │  HTTPS  │   Web 管理面板   │
│  C++ / Windows  │◄───────►│   (REST + WS)    │◄───────►│   Vue 3 SPA     │
│  (多个设备)      │         │                  │         │   (浏览器)       │
└─────────────────┘         └──────────────────┘         └─────────────────┘
                                     │
                            ┌────────┴────────┐
                            │   SQLite 数据库  │
                            │   (或 PostgreSQL)│
                            └─────────────────┘
```

**组件说明:**
- **后端**: 基于 Go 的 REST API + WebSocket 服务器，用于实时同步
- **前端**: Vue 3 Web 应用程序，用于剪贴板管理
- **数据库**: SQLite（默认）或 PostgreSQL（多用户场景）
- **反向代理**: Nginx 用于 SSL 终止和静态文件服务

## 快速开始

### 1. 开发环境（HTTP）

```bash
# 构建并启动
docker-compose up -d

# 查看日志
docker-compose logs -f

# 访问服务
# 前端: http://localhost
# 后端: http://localhost:8080
# 健康检查: http://localhost:8080/health
```

### 2. 生产环境（HTTPS）

#### 生成 SSL 证书

```bash
# Generate self-signed certificates (replace with your domain)
chmod +x generate-certs.sh
./generate-certs.sh "ditto.local"

# Or generate certificates for public domain
./generate-certs.sh "your-domain.com"
```

#### Configure JWT Secret

```bash
# Generate secure JWT secret
openssl rand -base64 32

# Create .env file
cat > .env << EOF
JWT_SECRET=$(openssl rand -base64 32)
EOF
```

#### Start Production Environment

```bash
docker-compose -f docker-compose.prod.yml up -d

# Access
# Frontend: https://localhost (or https://your-domain)
# Backend: https://localhost:8443
```

## Configuration Reference

### Environment Variables

| Variable | Description | Default |
|----------|-------------|---------|
| `PORT` | Backend server port | `8080` |
| `DATABASE_PATH` | SQLite database path | `/app/data/ditto_cloud.db` |
| `JWT_SECRET` | JWT signing secret key | `change-me-in-production` |
| `ADMIN_USERNAME` | Initial admin username (auto-create on first run) | None |
| `ADMIN_PASSWORD` | Initial admin password (min 6 chars) | None |
| `ADMIN_EMAIL` | Initial admin email | None |
| `TLS_CERT` | TLS certificate file path | None |
| `TLS_KEY` | TLS private key file path | None |
| `LOG_LEVEL` | Logging level (debug/info/warn/error) | `info` |
| `MAX_CLIP_SIZE` | Maximum clip size in bytes | `10485760` (10MB) |
| `SYNC_INTERVAL` | Background sync interval (seconds) | `30` |

### Docker Compose Files

**docker-compose.yml** - Development environment with HTTP
- No TLS, suitable for local development
- Ports: 80 (frontend), 8080 (backend)
- Hot-reload enabled for development

**docker-compose.prod.yml** - Production environment with HTTPS
- TLS/SSL enabled with certificate mounting
- Ports: 443 (frontend), 8443 (backend)
- Optimized for production use

### Docker Volumes

| Volume | Purpose | Backup Strategy |
|--------|---------|----------------|
| `ditto-data` | Database storage | Regular tar.gz backups |
| `certs` (bind mount) | SSL certificates | Copy to secure storage |

---

## Data Persistence & Backup

### Database Storage

All clipboard data is stored in the `ditto-data` Docker volume by default:

```bash
# Inspect the data volume
docker volume inspect ditto_ditto-data

# View database file
docker exec ditto-backend ls -la /app/data/
```

### Backup Procedures

```bash
# Backup database
docker run --rm -v ditto_ditto-data:/data -v $(pwd):/backup alpine \
  tar czf /backup/ditto-data-$(date +%Y%m%d-%H%M%S).tar.gz -C /data .

# Backup certificates (production)
cp -r certs/ certs-backup-$(date +%Y%m%d)/

# Automated backup script (add to cron)
#!/bin/bash
BACKUP_DIR="/path/to/backups"
DATE=$(date +%Y%m%d-%H%M%S)
docker run --rm -v ditto_ditto-data:/data -v $BACKUP_DIR:/backup alpine \
  tar czf /backup/ditto-$DATE.tar.gz -C /data .
# Keep only last 7 days
find $BACKUP_DIR -name "ditto-*.tar.gz" -mtime +7 -delete
```

### Restore from Backup

```bash
# Stop services
docker-compose down

# Restore database
docker run --rm -v ditto_ditto-data:/data -v $(pwd):/backup alpine \
  tar xzf /backup/ditto-data-20231201.tar.gz -C /data .

# Restart services
docker-compose up -d
```

### Database Migration

For multi-user scenarios, consider migrating to PostgreSQL:

```bash
# Set PostgreSQL connection in .env
DATABASE_URL=postgresql://user:pass@postgres-host:5432/ditto

# Run migration
docker-compose -f docker-compose.prod.yml up -d postgres
```

---

## SSL/TLS Certificate Management

### Self-Signed Certificates (Development)

```bash
./generate-certs.sh "ditto.local"
```

### Let's Encrypt Certificates (Production)

```bash
# Install Certbot
sudo apt-get install certbot

# Obtain certificate
sudo certbot certonly --standalone -d your-domain.com

# Copy certificates
sudo cp /etc/letsencrypt/live/your-domain.com/fullchain.pem certs/server.crt
sudo cp /etc/letsencrypt/live/your-domain.com/privkey.pem certs/server.key
```

### Certificate Auto-Renewal

```bash
# Add to crontab (renew 30 days before expiry)
0 3 * * * certbot renew --quiet --deploy-hook "cp /etc/letsencrypt/live/your-domain.com/fullchain.pem /path/to/certs/server.crt && cp /etc/letsencrypt/live/your-domain.com/privkey.pem /path/to/certs/server.key && docker restart ditto-frontend"
```

### Client Certificate Trust

#### Windows Clients

1. Copy `certs/ca.crt` to Windows machine
2. Double-click the certificate file
3. Click "Install Certificate"
4. Select "Local Machine"
5. Choose "Place all certificates in the following store"
6. Browse and select "Trusted Root Certification Authorities"
7. Complete installation

#### Linux Clients

```bash
sudo cp certs/ca.crt /usr/local/share/ca-certificates/
sudo update-ca-certificates
```

---
