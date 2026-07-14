# Stage 1: Build frontend
FROM node:20-alpine AS frontend

WORKDIR /app

COPY web/package*.json ./
RUN npm ci

COPY web/ ./
RUN npm run build

# Stage 2: Build Go backend
FROM golang:1.22-alpine AS backend

RUN apk add --no-cache git make gcc g++ musl-dev

WORKDIR /app

COPY server/go.mod server/go.sum ./
RUN go mod download

COPY server/ .
RUN CGO_ENABLED=1 GOOS=linux go build -ldflags="-s -w" -o /app/server ./cmd/server

# Stage 3: Run
FROM alpine:3.19

RUN apk add --no-cache ca-certificates tzdata

WORKDIR /app

COPY --from=frontend /app/dist /app/web-dist
COPY --from=backend /app/server /app/server

RUN mkdir -p /app/data

ENV PORT=8080
ENV DATABASE_PATH=/app/data/ditto_cloud.db

EXPOSE 8080

HEALTHCHECK --interval=30s --timeout=5s --start-period=10s --retries=3 \
  CMD wget --no-verbose --tries=1 --spider http://localhost:8080/health || exit 1

CMD ["/app/server"]