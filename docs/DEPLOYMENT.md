# Docker 部署指南

## 快速开始

### 1. 开发环境（HTTP）

```bash
# 构建并启动
docker-compose up -d

# 查看日志
docker-compose logs -f

# 访问
# 前端: http://localhost
# 后端: http://localhost:8080
# 健康检查: http://localhost:8080/health
```

### 2. 生产环境（HTTPS）

#### 生成自签名证书

```bash
# 生成证书（替换为你的域名）
chmod +x generate-certs.sh
./generate-certs.sh "ditto.local"

# 或生成公网域名证书
./generate-certs.sh "your-domain.com"
```

#### 配置 JWT 密钥

```bash
# 生成安全的 JWT 密钥
openssl rand -base64 32

# 创建 .env 文件
cat > .env << EOF
JWT_SECRET=$(openssl rand -base64 32)
EOF
```

#### 启动生产环境

```bash
docker-compose -f docker-compose.prod.yml up -d

# 访问
# 前端: https://localhost (或 https://your-domain)
# 后端: https://localhost:8443
```

## 配置说明

### 环境变量

| 变量 | 说明 | 默认值 |
|------|------|--------|
| `PORT` | 后端端口 | `8080` |
| `DATABASE_PATH` | 数据库路径 | `/app/data/ditto_cloud.db` |
| `JWT_SECRET` | JWT 密钥 | `change-me-in-production` |
| `TLS_CERT` | TLS 证书路径 | 无 |
| `TLS_KEY` | TLS 私钥路径 | 无 |

### 数据持久化

数据库存储在 Docker 卷 `ditto-data` 中：

```bash
# 查看数据卷
docker volume inspect ditto_ditto-data

# 备份数据库
docker run --rm -v ditto_ditto-data:/data -v $(pwd):/backup alpine tar czf /backup/ditto-data.tar.gz -C /data .

# 恢复数据库
docker run --rm -v ditto_ditto-data:/data -v $(pwd):/backup alpine tar xzf /backup/ditto-data.tar.gz -C /data
```

## 证书信任

### Windows 客户端信任自签名证书

1. 将 `certs/ca.crt` 复制到 Windows 机器
2. 双击证书
3. 点击"安装证书"
4. 选择"本地计算机"
5. 选择"将所有证书放入以下存储"
6. 浏览选择"受信任的根证书颁发机构"
7. 完成安装

### Linux 客户端信任

```bash
sudo cp certs/ca.crt /usr/local/share/ca-certificates/
sudo update-ca-certificates
```

## C++ 客户端配置

在 Ditto 设置中配置服务器地址：

- **开发环境**: `http://localhost:8080`
- **生产环境**: `https://your-domain:8443`

### 注册表配置

```
HKCU\Software\Ditto\CloudSync
  ServerUrl = "https://your-domain:8443"
  DeviceToken = "<从 Web 面板获取>"
  SyncEnabled = 1
```

### 便携版 INI 配置

```ini
[CloudSync]
ServerUrl=https://your-domain:8443
DeviceToken=<从 Web 面板获取>
SyncEnabled=1
```

## 维护

### 查看日志

```bash
# 所有服务
docker-compose logs -f

# 仅后端
docker-compose logs -f backend

# 仅前端
docker-compose logs -f frontend
```

### 更新服务

```bash
# 拉取最新代码
git pull

# 重新构建并启动
docker-compose up -d --build

# 生产环境
docker-compose -f docker-compose.prod.yml up -d --build
```

### 备份

```bash
# 备份数据库
docker run --rm -v ditto_ditto-data:/data -v $(pwd):/backup alpine \
  tar czf /backup/ditto-backup-$(date +%Y%m%d).tar.gz -C /data .

# 备份证书（生产环境）
cp -r certs/ certs-backup-$(date +%Y%m%d)/
```

### 清理

```bash
# 停止服务
docker-compose down

# 停止并删除数据卷（⚠️ 会丢失数据）
docker-compose down -v
```

## 故障排查

### 后端启动失败

```bash
# 检查日志
docker logs ditto-backend

# 检查数据库文件权限
docker exec ditto-backend ls -la /app/data/
```

### WebSocket 连接失败

确保 nginx 配置包含 WebSocket 代理设置：

```nginx
proxy_set_header Upgrade $http_upgrade;
proxy_set_header Connection "upgrade";
proxy_read_timeout 86400s;
```

### 证书错误

```bash
# 检查证书有效期
openssl x509 -in certs/server.crt -noout -dates

# 检查证书域名
openssl x509 -in certs/server.crt -noout -text | grep "Subject:"
```

## 性能优化

### 数据库 WAL 模式

已在代码中自动启用，无需额外配置。

### nginx 缓存

静态资源已配置 1 年缓存，无需修改。

### 内存限制

可在 `docker-compose.yml` 中添加资源限制：

```yaml
services:
  backend:
    deploy:
      resources:
        limits:
          memory: 256M
          cpus: '0.5'
```
