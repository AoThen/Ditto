-- Ditto Cloud Database - Initial Migration
-- GORM will auto-migrate on startup, this file documents the expected schema

-- 用户表
CREATE TABLE IF NOT EXISTS users (
    id              INTEGER PRIMARY KEY AUTOINCREMENT,
    username        TEXT UNIQUE NOT NULL,
    email           TEXT UNIQUE NOT NULL,
    password_hash   TEXT NOT NULL,
    status          INTEGER DEFAULT 1,
    created_at      DATETIME DEFAULT CURRENT_TIMESTAMP,
    updated_at      DATETIME DEFAULT CURRENT_TIMESTAMP
);

-- 设备表
CREATE TABLE IF NOT EXISTS devices (
    id              INTEGER PRIMARY KEY AUTOINCREMENT,
    user_id         INTEGER NOT NULL REFERENCES users(id),
    device_id       TEXT NOT NULL,
    device_name     TEXT,
    last_seen       DATETIME,
    created_at      DATETIME DEFAULT CURRENT_TIMESTAMP,
    UNIQUE(user_id, device_id)
);

-- 剪贴板主表
CREATE TABLE IF NOT EXISTS clips (
    id                  INTEGER PRIMARY KEY AUTOINCREMENT,
    user_id             INTEGER NOT NULL REFERENCES users(id),
    remote_clip_id      TEXT,
    description         TEXT,
    crc                 INTEGER,
    is_group            INTEGER DEFAULT 0,
    parent_id           INTEGER REFERENCES clips(id),
    clip_order          REAL,
    sticky_order        REAL,
    shortcut            INTEGER,
    global_shortcut     INTEGER,
    auto_delete         INTEGER DEFAULT 0,
    last_paste_date     DATETIME,
    source_device_id    INTEGER REFERENCES devices(id),
    encrypted           INTEGER DEFAULT 0,
    created_at          DATETIME DEFAULT CURRENT_TIMESTAMP,
    updated_at          DATETIME DEFAULT CURRENT_TIMESTAMP,
    deleted_at          DATETIME
);

CREATE INDEX IF NOT EXISTS idx_clips_user_crc ON clips(user_id, crc);
CREATE INDEX IF NOT EXISTS idx_clips_user_updated ON clips(user_id, updated_at DESC);
CREATE INDEX IF NOT EXISTS idx_clips_user_group ON clips(user_id, is_group, parent_id);

-- 剪贴板格式表
CREATE TABLE IF NOT EXISTS clip_formats (
    id              INTEGER PRIMARY KEY AUTOINCREMENT,
    clip_id         INTEGER NOT NULL REFERENCES clips(id) ON DELETE CASCADE,
    format_name     TEXT NOT NULL,
    data_type       TEXT DEFAULT 'inline',
    data            BLOB,
    data_size       INTEGER NOT NULL,
    created_at      DATETIME DEFAULT CURRENT_TIMESTAMP
);

CREATE INDEX IF NOT EXISTS idx_formats_clip ON clip_formats(clip_id);
CREATE INDEX IF NOT EXISTS idx_formats_name ON clip_formats(format_name);

-- 同步日志
CREATE TABLE IF NOT EXISTS sync_logs (
    id              INTEGER PRIMARY KEY AUTOINCREMENT,
    user_id         INTEGER NOT NULL REFERENCES users(id),
    device_id       INTEGER REFERENCES devices(id),
    action          TEXT NOT NULL,
    clip_count      INTEGER DEFAULT 0,
    status          TEXT DEFAULT 'success',
    error           TEXT,
    synced_at       DATETIME DEFAULT CURRENT_TIMESTAMP
);

CREATE INDEX IF NOT EXISTS idx_sync_logs_user_time ON sync_logs(user_id, synced_at DESC);

-- 用户设置
CREATE TABLE IF NOT EXISTS user_settings (
    user_id             INTEGER PRIMARY KEY REFERENCES users(id),
    max_clips           INTEGER DEFAULT 1000,
    max_storage_mb      INTEGER DEFAULT 100,
    auto_delete_days    INTEGER DEFAULT 30,
    encryption_enabled  INTEGER DEFAULT 1,
    encryption_salt     BLOB,
    language            TEXT DEFAULT 'zh-CN',
    updated_at          DATETIME DEFAULT CURRENT_TIMESTAMP
);

-- 限流记录表
CREATE TABLE IF NOT EXISTS rate_limit_records (
    key         TEXT PRIMARY KEY,
    fail_count  INTEGER DEFAULT 0,
    ban_until   DATETIME,
    updated_at  DATETIME DEFAULT CURRENT_TIMESTAMP
);

-- 加密设置表
CREATE TABLE IF NOT EXISTS encryption_settings (
    id              INTEGER PRIMARY KEY AUTOINCREMENT,
    user_id         INTEGER NOT NULL REFERENCES users(id),
    salt            BLOB NOT NULL,
    created_at      DATETIME DEFAULT CURRENT_TIMESTAMP,
    updated_at      DATETIME DEFAULT CURRENT_TIMESTAMP,
    UNIQUE(user_id)
);
