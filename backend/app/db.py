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
                last_updated TEXT    NOT NULL
            )
        """)
        conn.commit()
    finally:
        conn.close()


def load_all_colors() -> list[dict]:
    conn = _connect()
    try:
        rows = conn.execute(
            "SELECT mac, r, g, b, w, last_updated FROM colors"
        ).fetchall()
        return [dict(row) for row in rows]
    finally:
        conn.close()


def save_color(mac: str, r: int, g: int, b: int, w: int) -> None:
    now = datetime.now(timezone.utc).isoformat()
    conn = _connect()
    try:
        conn.execute(
            """
            INSERT INTO colors (mac, r, g, b, w, last_updated)
            VALUES (?, ?, ?, ?, ?, ?)
            ON CONFLICT(mac) DO UPDATE SET
                r            = excluded.r,
                g            = excluded.g,
                b            = excluded.b,
                w            = excluded.w,
                last_updated = excluded.last_updated
            """,
            (mac, r, g, b, w, now),
        )
        conn.commit()
    finally:
        conn.close()
