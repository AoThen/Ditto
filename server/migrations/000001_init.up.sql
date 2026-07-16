-- Migration 000001: Initial schema
-- 首次部署时由 AutoMigrate 自动创建所有表，
-- 此文件仅作为初始版本参考标记。

CREATE TABLE IF NOT EXISTS schema_migrations (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    version VARCHAR(50) NOT NULL UNIQUE,
    applied_at DATETIME DEFAULT CURRENT_TIMESTAMP
);

INSERT INTO schema_migrations (version) VALUES ('000001_init');