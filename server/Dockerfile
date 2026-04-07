# Stage 1: Build
FROM golang:1.21-alpine AS builder

RUN apk add --no-cache git make gcc g++ musl-dev

WORKDIR /app

COPY go.mod go.sum ./
RUN go mod download

COPY . .

RUN CGO_ENABLED=1 GOOS=linux go build -ldflags="-s -w" -o /app/server ./cmd/server

# Stage 2: Run
FROM alpine:3.19

RUN apk add --no-cache ca-certificates tzdata

WORKDIR /app

COPY --from=builder /app/server /app/server

RUN mkdir -p /app/data

ENV PORT=8080
ENV DATABASE_PATH=/app/data/ditto_cloud.db
ENV JWT_SECRET=change-me-in-production

EXPOSE 8080

HEALTHCHECK --interval=30s --timeout=5s --start-period=10s --retries=3 \
  CMD wget --no-verbose --tries=1 --spider http://localhost:8080/health || exit 1

CMD ["/app/server"]
