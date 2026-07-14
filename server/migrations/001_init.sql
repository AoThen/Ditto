-- Ditto Cloud Database - Initial Migration
-- GORM will auto-migrate on startup, this file documents the expected schema

-- 用户表
CREATE TABLE IF NOT EXISTS users (
    id              INTEGER PRIMARY KEY AUTOINCREMENT,
    username        TEXT NOT NULL UNIQUE,
    email           TEXT NOT NULL UNIQUE,
    password_hash   TEXT NOT NULL,
    role            TEXT DEFAULT 'user',
    is_active       INTEGER DEFAULT 1,
    created_at      DATETIME DEFAULT CURRENT_TIMESTAMP,
    updated_at      DATETIME DEFAULT CURRENT_TIMESTAMP
);

-- 设备表
CREATE TABLE IF NOT EXISTS devices (
    id              TEXT PRIMARY KEY,
    user_id         INTEGER NOT NULL REFERENCES users(id),
    device_name     TEXT NOT NULL,
    last_seen       DATETIME,
    created_at      DATETIME DEFAULT CURRENT_TIMESTAMP,
    token_version   INTEGER DEFAULT 0,
    UNIQUE(user_id, device_name)
);

-- 剪贴板主表
CREATE TABLE IF NOT EXISTS clips (
    id                TEXT PRIMARY KEY,
    user_id           INTEGER NOT NULL REFERENCES users(id),
    device_id         TEXT,
    description       TEXT,
    crc               INTEGER,
    created_at        DATETIME DEFAULT CURRENT_TIMESTAMP,
    updated_at        DATETIME DEFAULT CURRENT_TIMESTAMP,
    deleted_at        DATETIME,
    group_id          TEXT,
    short_cut         INTEGER DEFAULT 0,
    paste_count       INTEGER DEFAULT 0,
    is_conflict_copy  INTEGER DEFAULT 0,
    win_clip_id       TEXT
);

CREATE INDEX IF NOT EXISTS idx_clips_user_crc ON clips(user_id, crc);
CREATE INDEX IF NOT EXISTS idx_clips_user_updated ON clips(user_id, updated_at);
CREATE INDEX IF NOT EXISTS idx_clips_user_group ON clips(user_id, group_id);
CREATE INDEX IF NOT EXISTS idx_clips_user_created ON clips(user_id, created_at);
CREATE INDEX IF NOT EXISTS idx_clips_user_conflict ON clips(user_id, is_conflict_copy);

-- 剪贴板格式表
CREATE TABLE IF NOT EXISTS clip_formats (
    id              INTEGER PRIMARY KEY AUTOINCREMENT,
    clip_id         TEXT NOT NULL REFERENCES clips(id) ON DELETE CASCADE,
    format_type     INTEGER NOT NULL,
    data            BLOB,
    encrypted       INTEGER DEFAULT 0,
    created_at      DATETIME DEFAULT CURRENT_TIMESTAMP
);

CREATE INDEX IF NOT EXISTS idx_clip_formats_clip_fmt ON clip_formats(clip_id, format_type);

-- 分组表
CREATE TABLE IF NOT EXISTS groups (
    id              TEXT PRIMARY KEY,
    user_id         INTEGER NOT NULL REFERENCES users(id),
    name            TEXT NOT NULL,
    description     TEXT,
    parent_id       TEXT,
    clip_order      REAL DEFAULT 0,
    created_at      DATETIME DEFAULT CURRENT_TIMESTAMP,
    updated_at      DATETIME DEFAULT CURRENT_TIMESTAMP
);

CREATE INDEX IF NOT EXISTS idx_groups_user ON groups(user_id);

-- 同步日志
CREATE TABLE IF NOT EXISTS sync_logs (
    id              INTEGER PRIMARY KEY AUTOINCREMENT,
    user_id         INTEGER NOT NULL REFERENCES users(id),
    device_id       TEXT,
    action          TEXT NOT NULL,
    clip_count      INTEGER DEFAULT 0,
    status          TEXT DEFAULT 'success',
    error           TEXT,
    synced_at       DATETIME DEFAULT CURRENT_TIMESTAMP
);

CREATE INDEX IF NOT EXISTS idx_sync_logs_user_time ON sync_logs(user_id, synced_at);
CREATE INDEX IF NOT EXISTS idx_sync_logs_device ON sync_logs(device_id);

-- 限流记录表
CREATE TABLE IF NOT EXISTS rate_limit_records (
    key         TEXT PRIMARY KEY,
    fail_count  INTEGER DEFAULT 0,
    ban_until   DATETIME,
    updated_at  DATETIME DEFAULT CURRENT_TIMESTAMP
);

-- 加密设置表
CREATE TABLE IF NOT EXISTS encryption_settings (
    id                INTEGER PRIMARY KEY AUTOINCREMENT,
    user_id           INTEGER NOT NULL UNIQUE REFERENCES users(id),
    salt              BLOB NOT NULL,
    wrapped_dek       BLOB NOT NULL,
    verification_hash BLOB NOT NULL,
    password_hint     TEXT,
    enabled           INTEGER DEFAULT 0,
    version           INTEGER DEFAULT 2,
    created_at        DATETIME DEFAULT CURRENT_TIMESTAMP,
    updated_at        DATETIME DEFAULT CURRENT_TIMESTAMP
);