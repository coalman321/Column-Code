import os
import sqlite3
from datetime import datetime, timezone
from pathlib import Path

_DEFAULT_DB = Path(__file__).parent.parent / "data" / "column.db"
DB_PATH = Path(os.getenv("DB_PATH", str(_DEFAULT_DB)))


def _connect() -> sqlite3.Connection:
    DB_PATH.parent.mkdir(parents=True, exist_ok=True)
    conn = sqlite3.connect(str(DB_PATH), check_same_thread=False)
    conn.row_factory = sqlite3.Row
    return conn


def init_db() -> None:
    conn = _connect()
    try:
        conn.execute("""
            CREATE TABLE IF NOT EXISTS colors (
                mac          TEXT PRIMARY KEY,
                r            INTEGER NOT NULL DEFAULT 0,
                g            INTEGER NOT NULL DEFAULT 0,
                b            INTEGER NOT NULL DEFAULT 0,
                w            INTEGER NOT NULL DEFAULT 0,
                flicker      INTEGER NOT NULL DEFAULT 0,
                last_updated TEXT    NOT NULL
            )
        """)
        conn.execute("""
            CREATE TABLE IF NOT EXISTS clients (
                mac       TEXT PRIMARY KEY,
                last_seen TEXT NOT NULL
            )
        """)
        conn.execute("""
            CREATE TABLE IF NOT EXISTS presets (
                id      INTEGER PRIMARY KEY AUTOINCREMENT,
                name    TEXT    NOT NULL UNIQUE,
                r       INTEGER NOT NULL,
                g       INTEGER NOT NULL,
                b       INTEGER NOT NULL,
                w       INTEGER NOT NULL,
                flicker INTEGER NOT NULL DEFAULT 0
            )
        """)
        conn.commit()
    finally:
        conn.close()


def load_all_clients() -> list[dict]:
    conn = _connect()
    try:
        rows = conn.execute("SELECT mac, last_seen FROM clients").fetchall()
        return [dict(row) for row in rows]
    finally:
        conn.close()


def save_client(mac: str, last_seen: datetime) -> None:
    conn = _connect()
    try:
        conn.execute(
            """
            INSERT INTO clients (mac, last_seen)
            VALUES (?, ?)
            ON CONFLICT(mac) DO UPDATE SET last_seen = excluded.last_seen
            """,
            (mac, last_seen.isoformat()),
        )
        conn.commit()
    finally:
        conn.close()


def delete_client(mac: str) -> None:
    conn = _connect()
    try:
        conn.execute("DELETE FROM clients WHERE mac = ?", (mac,))
        conn.commit()
    finally:
        conn.close()


def load_all_colors() -> list[dict]:
    conn = _connect()
    try:
        rows = conn.execute(
            "SELECT mac, r, g, b, w, flicker, last_updated FROM colors"
        ).fetchall()
        return [dict(row) for row in rows]
    finally:
        conn.close()


def delete_color(mac: str) -> None:
    conn = _connect()
    try:
        conn.execute("DELETE FROM colors WHERE mac = ?", (mac,))
        conn.commit()
    finally:
        conn.close()


def save_color(mac: str, r: int, g: int, b: int, w: int, flicker: bool = False) -> None:
    now = datetime.now(timezone.utc).isoformat()
    conn = _connect()
    try:
        conn.execute(
            """
            INSERT INTO colors (mac, r, g, b, w, flicker, last_updated)
            VALUES (?, ?, ?, ?, ?, ?, ?)
            ON CONFLICT(mac) DO UPDATE SET
                r            = excluded.r,
                g            = excluded.g,
                b            = excluded.b,
                w            = excluded.w,
                flicker      = excluded.flicker,
                last_updated = excluded.last_updated
            """,
            (mac, r, g, b, w, 1 if flicker else 0, now),
        )
        conn.commit()
    finally:
        conn.close()


def load_all_presets() -> list[dict]:
    conn = _connect()
    try:
        rows = conn.execute("SELECT id, name, r, g, b, w, flicker FROM presets ORDER BY name").fetchall()
        return [dict(row) for row in rows]
    finally:
        conn.close()


def save_preset(name: str, r: int, g: int, b: int, w: int, flicker: bool = False) -> int:
    conn = _connect()
    try:
        cursor = conn.execute(
            "INSERT INTO presets (name, r, g, b, w, flicker) VALUES (?, ?, ?, ?, ?, ?)",
            (name, r, g, b, w, 1 if flicker else 0),
        )
        conn.commit()
        return cursor.lastrowid
    finally:
        conn.close()


def delete_preset(preset_id: int) -> None:
    conn = _connect()
    try:
        conn.execute("DELETE FROM presets WHERE id = ?", (preset_id,))
        conn.commit()
    finally:
        conn.close()
