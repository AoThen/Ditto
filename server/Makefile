.PHONY: build run test clean swagger docker

# 构建
build:
	@echo "Building server..."
	CGO_ENABLED=1 go build -o bin/server cmd/server/main.go

# 运行（开发模式）
run:
	@echo "Starting server in debug mode..."
	go run cmd/server/main.go

# 测试
test:
	@echo "Running tests..."
	go test ./... -v -cover

# 清理
clean:
	rm -rf bin/
	rm -f server

# 生成 Swagger 文档
swagger:
	@echo "Generating Swagger documentation..."
	swag init -g cmd/server/main.go -o api

# 构建 Docker 镜像
docker:
	docker build -t ditto-cloud-server .

# 运行 Docker
docker-run:
	docker run -d -p 8080:8080 \
		-v ./data:/app/data \
		-e DB_PATH=/app/data/ditto_cloud.db \
		-e JWT_SECRET=dev-secret-key \
		ditto-cloud-server

# 数据库迁移（查看 SQL）
migration:
	@echo "Initial migration SQL is in migrations/001_init.sql"

# 帮助
help:
	@echo "Ditto Cloud Server - Makefile"
	@echo ""
	@echo "Usage:"
	@echo "  make build      - Build binary"
	@echo "  make run        - Run in debug mode"
	@echo "  make test       - Run tests"
	@echo "  make clean      - Remove build artifacts"
	@echo "  make swagger    - Generate Swagger docs"
	@echo "  make docker     - Build Docker image"
	@echo "  make docker-run - Run Docker container"
	@echo "  make migration  - Show migration info"
