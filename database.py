"""
Database layer for Reliable IoT Data Transmission System
Handles SQLite database initialization, record insertion, deduplication,
and query execution for live and recovered offline sensor data.
"""

import sqlite3
import os
from datetime import datetime

DATABASE_DIR = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "database")
DATABASE_PATH = os.path.join(DATABASE_DIR, "sensor_data.db")


def get_connection():
    """Returns a SQLite connection with row factory configured."""
    os.makedirs(DATABASE_DIR, exist_ok=True)
    conn = sqlite3.connect(DATABASE_PATH, timeout=10)
    conn.row_factory = sqlite3.Row
    return conn


def init_db():
    """Initializes database tables and indexes."""
    os.makedirs(DATABASE_DIR, exist_ok=True)
    conn = get_connection()
    cursor = conn.cursor()

    # Sensor Readings Table
    # record_id is UNIQUE to guarantee idempotent synchronization from LittleFS
    cursor.execute("""
        CREATE TABLE IF NOT EXISTS readings (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            record_id TEXT UNIQUE NOT NULL,
            device_id TEXT NOT NULL,
            temperature REAL NOT NULL,
            humidity REAL NOT NULL,
            timestamp INTEGER NOT NULL,
            iso_timestamp TEXT NOT NULL,
            is_offline_recovered INTEGER DEFAULT 0,
            server_received_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
        )
    """)

    # Index for fast chronological queries and duplicate lookups
    cursor.execute("""
        CREATE INDEX IF NOT EXISTS idx_readings_timestamp ON readings(timestamp ASC)
    """)
    cursor.execute("""
        CREATE INDEX IF NOT EXISTS idx_readings_record_id ON readings(record_id)
    """)
    cursor.execute("""
        CREATE INDEX IF NOT EXISTS idx_readings_recovered ON readings(is_offline_recovered)
    """)

    # Device Status Table
    cursor.execute("""
        CREATE TABLE IF NOT EXISTS device_status (
            device_id TEXT PRIMARY KEY,
            status TEXT DEFAULT 'OFFLINE',
            last_seen TIMESTAMP,
            last_temp REAL,
            last_humidity REAL,
            total_readings INTEGER DEFAULT 0,
            total_recovered INTEGER DEFAULT 0,
            buzzer_state TEXT DEFAULT 'OFF'
        )
    """)

    # Network Outage and System Events Table
    cursor.execute("""
        CREATE TABLE IF NOT EXISTS network_events (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            event_type TEXT NOT NULL,
            device_id TEXT,
            timestamp TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
            details TEXT
        )
    """)

    conn.commit()
    conn.close()


def insert_reading(data):
    """
    Inserts a single sensor reading.
    Returns: (inserted: bool, message: str)
    """
    conn = get_connection()
    cursor = conn.cursor()

    record_id = data.get("record_id")
    device_id = data.get("device_id", "ESP32_DEFAULT")
    temperature = float(data.get("temperature", 0.0))
    humidity = float(data.get("humidity", 0.0))
    timestamp = int(data.get("timestamp", int(datetime.utcnow().timestamp())))
    iso_timestamp = data.get("iso_timestamp", datetime.utcnow().isoformat() + "Z")
    is_recovered = int(data.get("is_offline_recovered", 0))

    try:
        # INSERT OR IGNORE to prevent duplicate entries
        cursor.execute("""
            INSERT OR IGNORE INTO readings 
            (record_id, device_id, temperature, humidity, timestamp, iso_timestamp, is_offline_recovered)
            VALUES (?, ?, ?, ?, ?, ?, ?)
        """, (record_id, device_id, temperature, humidity, timestamp, iso_timestamp, is_recovered))

        inserted = cursor.rowcount > 0

        # Update Device Status
        now_str = datetime.utcnow().isoformat() + "Z"
        cursor.execute("""
            INSERT INTO device_status (device_id, status, last_seen, last_temp, last_humidity, total_readings, total_recovered, buzzer_state)
            VALUES (?, 'ONLINE', ?, ?, ?, 1, ?, 'OFF')
            ON CONFLICT(device_id) DO UPDATE SET
                status = 'ONLINE',
                last_seen = excluded.last_seen,
                last_temp = excluded.last_temp,
                last_humidity = excluded.last_humidity,
                total_readings = total_readings + (CASE WHEN ? THEN 1 ELSE 0 END),
                total_recovered = total_recovered + (CASE WHEN ? AND ? = 1 THEN 1 ELSE 0 END),
                buzzer_state = 'OFF'
        """, (device_id, now_str, temperature, humidity, is_recovered, inserted, inserted, is_recovered))

        conn.commit()
        conn.close()

        if inserted:
            return True, "Record inserted successfully"
        else:
            return False, "Duplicate record skipped (already exists)"

    except Exception as e:
        conn.rollback()
        conn.close()
        return False, f"Database error: {str(e)}"


def insert_batch_readings(records, device_id="ESP32_DEFAULT"):
    """
    Inserts an array of offline recovered readings in a single transaction.
    Returns: (inserted_count: int, acknowledged_ids: list[str])
    """
    conn = get_connection()
    cursor = conn.cursor()

    acknowledged_ids = []
    inserted_count = 0

    try:
        for item in records:
            rec_id = item.get("record_id")
            dev_id = item.get("device_id", device_id)
            temperature = float(item.get("temperature", 0.0))
            humidity = float(item.get("humidity", 0.0))
            timestamp = int(item.get("timestamp", int(datetime.utcnow().timestamp())))
            iso_timestamp = item.get("iso_timestamp", datetime.utcnow().isoformat() + "Z")
            is_recovered = 1  # batch upload from LittleFS is inherently recovered data

            cursor.execute("""
                INSERT OR IGNORE INTO readings 
                (record_id, device_id, temperature, humidity, timestamp, iso_timestamp, is_offline_recovered)
                VALUES (?, ?, ?, ?, ?, ?, ?)
            """, (rec_id, dev_id, temperature, humidity, timestamp, iso_timestamp, is_recovered))

            if cursor.rowcount > 0:
                inserted_count += 1
            
            # Acknowledge all processed valid IDs so ESP32 knows it doesn't need to re-send
            acknowledged_ids.append(rec_id)

        # Update aggregate counts in device_status
        now_str = datetime.utcnow().isoformat() + "Z"
        if records:
            last_record = records[-1]
            cursor.execute("""
                INSERT INTO device_status (device_id, status, last_seen, last_temp, last_humidity, total_readings, total_recovered, buzzer_state)
                VALUES (?, 'ONLINE', ?, ?, ?, ?, ?, 'OFF')
                ON CONFLICT(device_id) DO UPDATE SET
                    status = 'ONLINE',
                    last_seen = excluded.last_seen,
                    total_readings = total_readings + ?,
                    total_recovered = total_recovered + ?,
                    buzzer_state = 'OFF'
            """, (
                device_id, now_str, 
                float(last_record.get("temperature", 0.0)), 
                float(last_record.get("humidity", 0.0)),
                inserted_count, inserted_count,
                inserted_count, inserted_count
            ))

        # Log network synchronization event
        if inserted_count > 0:
            cursor.execute("""
                INSERT INTO network_events (event_type, device_id, details)
                VALUES ('SYNC_RECOVERY', ?, ?)
            """, (device_id, f"Recovered {inserted_count} pending offline records from ESP32 LittleFS"))

        conn.commit()
        conn.close()
        return inserted_count, acknowledged_ids

    except Exception as e:
        conn.rollback()
        conn.close()
        raise e


def get_latest_reading(device_id=None):
    """Retrieves the most recent sensor reading."""
    conn = get_connection()
    cursor = conn.cursor()

    if device_id:
        cursor.execute("""
            SELECT * FROM readings WHERE device_id = ? ORDER BY timestamp DESC, id DESC LIMIT 1
        """, (device_id,))
    else:
        cursor.execute("SELECT * FROM readings ORDER BY timestamp DESC, id DESC LIMIT 1")

    row = cursor.fetchone()
    conn.close()
    return dict(row) if row else None


def get_history(limit=100, device_id=None):
    """Retrieves chronological history of sensor readings."""
    conn = get_connection()
    cursor = conn.cursor()

    if device_id:
        cursor.execute("""
            SELECT * FROM (
                SELECT * FROM readings WHERE device_id = ? ORDER BY timestamp DESC, id DESC LIMIT ?
            ) ORDER BY timestamp ASC
        """, (device_id, limit))
    else:
        cursor.execute("""
            SELECT * FROM (
                SELECT * FROM readings ORDER BY timestamp DESC, id DESC LIMIT ?
            ) ORDER BY timestamp ASC
        """, (limit,))

    rows = cursor.fetchall()
    conn.close()
    return [dict(r) for r in rows]


def get_recovered_readings(limit=100):
    """Retrieves records specifically recovered from ESP32 LittleFS flash."""
    conn = get_connection()
    cursor = conn.cursor()
    cursor.execute("""
        SELECT * FROM readings 
        WHERE is_offline_recovered = 1 
        ORDER BY timestamp DESC LIMIT ?
    """, (limit,))
    rows = cursor.fetchall()
    conn.close()
    return [dict(r) for r in rows]


def get_device_status(device_id=None):
    """
    Retrieves current device status.
    Automatically detects timeout if last_seen > 25 seconds ago (2.5x sampling interval).
    """
    conn = get_connection()
    cursor = conn.cursor()

    if device_id:
        cursor.execute("SELECT * FROM device_status WHERE device_id = ?", (device_id,))
    else:
        cursor.execute("SELECT * FROM device_status ORDER BY last_seen DESC LIMIT 1")

    row = cursor.fetchone()
    if not row:
        conn.close()
        return {
            "device_id": device_id or "ESP32_NODE_1",
            "status": "OFFLINE",
            "last_seen": None,
            "seconds_since_last_seen": None,
            "last_temp": None,
            "last_humidity": None,
            "total_readings": 0,
            "total_recovered": 0,
            "buzzer_state": "OFF"
        }

    status_dict = dict(row)

    # Check timeout
    if status_dict.get("last_seen"):
        try:
            last_dt = datetime.fromisoformat(status_dict["last_seen"].replace("Z", "+00:00"))
            delta = (datetime.utcnow() - last_dt.replace(tzinfo=None)).total_seconds()
            status_dict["seconds_since_last_seen"] = round(delta, 1)

            # If no reading in > 25 seconds (ESP32 sample period is 10s), mark OFFLINE
            if delta > 25.0:
                if status_dict["status"] != "OFFLINE":
                    cursor.execute("""
                        UPDATE device_status SET status = 'OFFLINE' WHERE device_id = ?
                    """, (status_dict["device_id"],))
                    cursor.execute("""
                        INSERT INTO network_events (event_type, device_id, details)
                        VALUES ('DISCONNECTED', ?, ?)
                    """, (status_dict["device_id"], f"Device communication timeout ({round(delta, 1)}s since last transmission)"))
                    conn.commit()
                status_dict["status"] = "OFFLINE"
        except Exception:
            pass

    conn.close()
    return status_dict


def get_network_events(limit=20):
    """Retrieves recent network disconnection and recovery sync events."""
    conn = get_connection()
    cursor = conn.cursor()
    cursor.execute("""
        SELECT * FROM network_events ORDER BY id DESC LIMIT ?
    """, (limit,))
    rows = cursor.fetchall()
    conn.close()
    return [dict(r) for r in rows]


def reset_database():
    """Wipes all data for fresh demo / hackathon presentation."""
    conn = get_connection()
    cursor = conn.cursor()
    cursor.execute("DELETE FROM readings")
    cursor.execute("DELETE FROM device_status")
    cursor.execute("DELETE FROM network_events")
    cursor.execute("DELETE FROM sqlite_sequence")
    conn.commit()
    conn.close()
