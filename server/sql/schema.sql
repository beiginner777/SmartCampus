-- M1 建表脚本（PostgreSQL）
--   psql -U postgres -c "CREATE DATABASE smart_campus;"
-- 再在本库执行本脚本：
--   psql -U postgres -d smart_campus -f sql/schema.sql

CREATE TABLE IF NOT EXISTS users (
    id BIGSERIAL PRIMARY KEY,
    username VARCHAR(64) NOT NULL UNIQUE,
    password_hash VARCHAR(128) NOT NULL,      -- PBKDF2-HMAC-SHA256 存储串
    role SMALLINT NOT NULL DEFAULT 0,         -- 0=student 1=teacher 2=manager
    nickname VARCHAR(64) NOT NULL DEFAULT '',
    tags VARCHAR(255) NOT NULL DEFAULT '',
    created_at TIMESTAMP NOT NULL DEFAULT now()
);
