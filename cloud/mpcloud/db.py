"""MP Cloud database (SQLite). One file, safe for the threads the app uses."""
import json
import os
import sqlite3
import threading
import time

SCHEMA = """
CREATE TABLE IF NOT EXISTS users(
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  email TEXT UNIQUE NOT NULL,
  name TEXT NOT NULL,
  pw_hash TEXT NOT NULL,
  is_admin INTEGER NOT NULL DEFAULT 0,
  ocpu_limit REAL NOT NULL,
  mem_limit REAL NOT NULL,
  disk_limit INTEGER NOT NULL,
  inst_limit INTEGER NOT NULL,
  created REAL NOT NULL
);
CREATE TABLE IF NOT EXISTS sessions(
  token_hash TEXT PRIMARY KEY,
  user_id INTEGER NOT NULL REFERENCES users(id) ON DELETE CASCADE,
  expires REAL NOT NULL
);
CREATE TABLE IF NOT EXISTS api_keys(
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  user_id INTEGER NOT NULL REFERENCES users(id) ON DELETE CASCADE,
  name TEXT NOT NULL,
  prefix TEXT NOT NULL,
  token_hash TEXT UNIQUE NOT NULL,
  created REAL NOT NULL,
  last_used REAL
);
CREATE TABLE IF NOT EXISTS instances(
  id TEXT PRIMARY KEY,
  user_id INTEGER NOT NULL REFERENCES users(id) ON DELETE CASCADE,
  name TEXT NOT NULL,
  backend TEXT NOT NULL DEFAULT 'local',
  backend_name TEXT UNIQUE NOT NULL,
  preset TEXT NOT NULL DEFAULT 'plain',
  shape TEXT NOT NULL DEFAULT 'MP.Local.Flex',
  status_note TEXT,
  image TEXT NOT NULL,
  ocpus REAL NOT NULL,
  mem_gb REAL NOT NULL,
  disk_gb INTEGER NOT NULL,
  mp_region TEXT,
  ssh_key TEXT,
  state TEXT NOT NULL,
  ipv4 TEXT,
  ports TEXT NOT NULL DEFAULT '[]',
  error TEXT,
  extra TEXT,
  created REAL NOT NULL
);
CREATE TABLE IF NOT EXISTS audit(
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  user_id INTEGER,
  at REAL NOT NULL,
  action TEXT NOT NULL,
  target TEXT,
  detail TEXT
);
CREATE TABLE IF NOT EXISTS settings(key TEXT PRIMARY KEY, value TEXT NOT NULL);
"""

DEFAULT_SETTINGS = {
    "signup": "invite",          # open | invite | closed
    "invite_code": "",
    "region_label": "ap-singapore-1",
    "public_host": "",           # IP or name players / SSH use to reach this machine
    "free_ocpus": "2",           # "Always Free" quota for new accounts (same as Oracle A1 in 2026)
    "free_mem_gb": "12",
    "free_disk_gb": "100",
    "free_instances": "2",
}


class DB:
    def __init__(self, path):
        d = os.path.dirname(os.path.abspath(path))
        os.makedirs(d, exist_ok=True)
        self.path = path
        self.lock = threading.RLock()
        self.conn = sqlite3.connect(path, check_same_thread=False, isolation_level=None)
        self.conn.row_factory = sqlite3.Row
        self.conn.execute("PRAGMA foreign_keys=ON")
        self.conn.execute("PRAGMA journal_mode=WAL")
        self.conn.executescript(SCHEMA)
        for k, v in DEFAULT_SETTINGS.items():
            self.conn.execute("INSERT OR IGNORE INTO settings(key,value) VALUES(?,?)", (k, v))
        try:
            os.chmod(path, 0o600)
        except OSError:
            pass

    def q(self, sql, args=()):
        with self.lock:
            return [dict(r) for r in self.conn.execute(sql, args).fetchall()]

    def one(self, sql, args=()):
        r = self.q(sql, args)
        return r[0] if r else None

    def x(self, sql, args=()):
        with self.lock:
            cur = self.conn.execute(sql, args)
            return cur.lastrowid

    # settings
    def settings(self):
        return {r["key"]: r["value"] for r in self.q("SELECT key,value FROM settings")}

    def set_setting(self, k, v):
        self.x("INSERT INTO settings(key,value) VALUES(?,?) ON CONFLICT(key) DO UPDATE SET value=excluded.value", (k, str(v)))

    # audit
    def log(self, user_id, action, target="", detail=""):
        if not isinstance(detail, str):
            detail = json.dumps(detail)
        self.x("INSERT INTO audit(user_id,at,action,target,detail) VALUES(?,?,?,?,?)",
               (user_id, time.time(), action, target, detail))
