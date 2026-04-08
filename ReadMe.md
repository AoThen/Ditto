# [Ditto - 剪贴板管理器](https://github.com/sabrogden/Ditto/releases/download/3.25.113.0/DittoSetup_3_25_113_0.exe)

![GitHub Downloads (all assets, latest release)](https://img.shields.io/github/downloads/sabrogden/Ditto/latest/total) ![GitHub commits since latest release](https://img.shields.io/github/commits-since/sabrogden/Ditto/latest) ![GitHub contributors](https://img.shields.io/github/contributors/sabrogten/Ditto)

---

**[English](#ditto---clipboard-manager)** | **[中文](#ditto---剪贴板管理器)**

---

# Ditto - Clipboard Manager

---

## 基本用法

1. 运行 Ditto
2. 复制内容到剪贴板，例如在文本编辑器中选中文字后按 Ctrl+C
3. 点击系统托盘中的 Ditto 图标，或按下默认快捷键 `Ctrl + ``（反引号键）打开 Ditto
4. 双击或按回车键将选中的内容粘贴到之前的窗口

## 本地优先
- ✅ 无需登录
- ✅ 无云端依赖
- ✅ 无遥测数据

## ☁️ 云端同步与 Web 管理面板（新功能！）

Ditto 现在支持**云端同步**和基于 Web 的管理面板，实现跨设备剪贴板管理！

### 特性
- ✅ **多设备同步** - 在设备间无缝同步剪贴板内容
- ✅ **Web 管理面板** - 从任何浏览器查看、搜索和管理剪贴板
- ✅ **端到端加密** - AES-256-GCM 加密保护您的数据隐私
- ✅ **实时同步** - 基于 WebSocket 的设备间即时同步
- ✅ **自托管** - 通过 Docker 部署，完全掌控您的数据

### 快速开始云端部署

```bash
# 1. 克隆仓库
git clone https://github.com/sabrogden/Ditto.git
cd Ditto

# 2. 启动开发环境
docker-compose up -d

# 3. 访问服务
# 前端: http://localhost
# 后端 API: http://localhost:8080
# 健康检查: http://localhost:8080/health
```

生产环境部署（HTTPS）：
```bash
# 生成 SSL 证书
./generate-certs.sh "your-domain.com"

# 配置 JWT 密钥
echo "JWT_SECRET=$(openssl rand -base64 32)" > .env

# 启动生产环境
docker-compose -f docker-compose.prod.yml up -d
```

📖 **完整文档：**
- [云端部署指南](docs/DEPLOYMENT.md)
- [云端服务器架构](docs/PROJECT_PLAN.md)
- [快速部署指南](DEPLOYMENT_GUIDE.md)
- [云端部署详解](docs/CLOUD_DEPLOYMENT.md)

## Windows 代码签名策略
Windows 二进制文件的免费代码签名由 SignPath.io 提供，证书由 SignPath Foundation 提供。
<br>
<br>

<img src="ditto.gif">

---

# Ditto - 剪贴板管理器（中文版）

Ditto 是 Windows 标准剪贴板的扩展。它会保存每次放置到剪贴板上的内容，允许您随后访问这些内容。Ditto 可以保存任何可以放置到剪贴板上的信息类型，包括文本、图片、HTML、自定义格式等。

## 下载

1. [安装程序](https://github.com/sabrogden/Ditto/releases/download/3.25.113.0/DittoSetup_3_25_113_0.exe)
2. [便携版](https://github.com/sabrogden/Ditto/releases/download/3.25.113.0/DittoPortable_3_25_113_0.zip)
3. [Chocolatey](https://chocolatey.org/packages/ditto/3.23.124.0) `choco install ditto`
4. [Chocolatey 便携版](https://chocolatey.org/packages/ditto.portable/3.23.124.0) `choco install ditto.portable`
5. [Winget](https://winget.run/pkg/Ditto/Ditto) `winget install -e --id Ditto.Ditto`
6. [Windows 应用商店](https://www.microsoft.com/en-us/store/p/ditto-cp/9nblggh3zbjq)



[Help/Wiki](https://github.com/sabrogden/Ditto/wiki)&nbsp; &nbsp; &nbsp; &nbsp; &nbsp;&nbsp; &nbsp; &nbsp; &nbsp; &nbsp;[Forums](https://github.com/sabrogden/Ditto/issues)&nbsp; &nbsp; &nbsp; &nbsp; &nbsp;&nbsp; &nbsp; &nbsp; &nbsp; &nbsp;[Donate](https://www.paypal.com/donate/?item_name=Donation+to+Ditto&cmd=_donations&business=sabrogden%40gmail.com&Z3JncnB0=)&nbsp; &nbsp; &nbsp; &nbsp; &nbsp;&nbsp; &nbsp; &nbsp; &nbsp; &nbsp;[Beta](https://ditto-cp.sourceforge.io/beta/)&nbsp; &nbsp; &nbsp; &nbsp; &nbsp;&nbsp; &nbsp; &nbsp; &nbsp; &nbsp;[Source](https://github.com/sabrogden/Ditto)&nbsp; &nbsp; &nbsp; &nbsp; &nbsp;&nbsp; &nbsp; &nbsp; &nbsp; &nbsp;[History](https://github.com/sabrogden/Ditto/releases)

Ditto is an extension to the standard windows clipboard. It saves each item placed on the clipboard allowing you access to any of those items at a later time. Ditto allows you to save any type of information that can be put on the clipboard, text, images, html, custom formats.

## Download

1. [Installer](https://github.com/sabrogden/Ditto/releases/download/3.25.113.0/DittoSetup_3_25_113_0.exe)
2. [Portable](https://github.com/sabrogden/Ditto/releases/download/3.25.113.0/DittoPortable_3_25_113_0.zip)
3. [Chocolatey](https://chocolatey.org/packages/ditto/3.23.124.0) choco install ditto
4. [Chocolatey Portable](https://chocolatey.org/packages/ditto.portable/3.23.124.0) choco install ditto.portable
5. [Winget](https://winget.run/pkg/Ditto/Ditto) winget install -e --id Ditto.Ditto
6. [Windows Store App](https://www.microsoft.com/en-us/store/p/ditto-cp/9nblggh3zbjq)


## Basic Usage

1. Run Ditto
2. Copy things to the clipboard, e.g. using Ctrl-C with text selected in a text editor.
3. Open Ditto by clicking its icon in the system tray or by pressing its Hot Key which defaults to Ctrl + ` – i.e. hold down Ctrl and press the back-quote (tilde ~) key.
4. Double click or press enter on the item to paste it to the previous window.

## Local First
- No login
- No cloud
- No telemetry

## ☁️ Cloud Sync & Web Panel (New!)

Ditto now supports **cloud synchronization** and a **web-based management panel** for cross-device clipboard management!

### Features
- ✅ **Multi-device sync** - Seamlessly sync clips across devices
- ✅ **Web management panel** - View, search, and manage clips from any browser
- ✅ **End-to-end encryption** - Your data stays private with AES-256-GCM encryption
- ✅ **Real-time sync** - WebSocket-based instant sync between devices
- ✅ **Self-hosted** - Full control over your data with Docker deployment

### Quick Start Cloud Deployment

```bash
# 1. Clone the repository
git clone https://github.com/sabrogden/Ditto.git
cd Ditto

# 2. Start development environment
docker-compose up -d

# 3. Access services
# Frontend: http://localhost
# Backend API: http://localhost:8080
# Health check: http://localhost:8080/health
```

For production deployment with HTTPS:
```bash
# Generate SSL certificates
./generate-certs.sh "your-domain.com"

# Configure JWT secret
echo "JWT_SECRET=$(openssl rand -base64 32)" > .env

# Start production environment
docker-compose -f docker-compose.prod.yml up -d
```

📖 **Full documentation:**
- [Cloud Deployment Guide](docs/DEPLOYMENT.md)
- [Cloud Server Architecture](docs/PROJECT_PLAN.md)
- [Quick Deployment Guide](DEPLOYMENT_GUIDE.md)

## Windows Code-Signing Policy
Free code signing on Windows binaries provided by SignPath.io, certificate by SignPath Foundation.
<br>
<br>

<img src="ditto.gif">

