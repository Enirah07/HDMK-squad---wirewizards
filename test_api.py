"""
Automated Integration Test Suite for Reliable IoT Backend
Tests all endpoints, SQLite transactions, duplicate prevention,
and LittleFS batch synchronization using Flask test client.
"""

import sys
import os
import json
import time

# Ensure backend directory is in path
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from app import app
import database

def run_tests():
    print("==================================================")
    print("STARTING RELIABLE IoT BACKEND VERIFICATION SUITE")
    print("==================================================")
    client = app.test_client()

    # Reset DB
    database.reset_database()
    print("[1] Database wiped for clean test run.")

    # 1. Health check
    res = client.get("/api/device/health")
    assert res.status_code == 200, f"Health check failed: {res.status_code}"
    data = res.get_json()
    assert data["status"] == "ok"
    print(f"[PASS] /api/device/health -> OK (Server time: {data['server_time_iso']})")

    # 2. Ingest Single Live Reading
    live_payload = {
        "record_id": "ESP32_E465B8_BOOT1_0001",
        "device_id": "ESP32_NODE_1",
        "temperature": 28.4,
        "humidity": 62.5,
        "timestamp": int(time.time()),
        "iso_timestamp": "2026-10-26T11:25:00Z",
        "is_offline_recovered": 0
    }
    res = client.post("/api/readings", json=live_payload)
    assert res.status_code == 201, f"Live reading insert failed: {res.status_code}, {res.data}"
    data = res.get_json()
    assert data["status"] == "success"
    print(f"[PASS] POST /api/readings (Live) -> Created (Record: {data['record_id']})")

    # 3. Duplicate Prevention Test
    res_dup = client.post("/api/readings", json=live_payload)
    assert res_dup.status_code == 200, f"Duplicate test unexpected status: {res_dup.status_code}"
    dup_data = res_dup.get_json()
    assert dup_data["status"] == "skipped"
    print(f"[PASS] POST /api/readings (Duplicate) -> Safely Skipped without error")

    # 4. LittleFS Batch Recovery Synchronization Test
    batch_payload = {
        "device_id": "ESP32_NODE_1",
        "readings": [
            {
                "record_id": "ESP32_E465B8_BOOT1_0002",
                "temperature": 32.1,
                "humidity": 64.0,
                "timestamp": int(time.time()) - 30,
                "iso_timestamp": "2026-10-26T11:25:10Z"
            },
            {
                "record_id": "ESP32_E465B8_BOOT1_0003",
                "temperature": 36.8,  # Critical threshold > 35°C
                "humidity": 65.2,
                "timestamp": int(time.time()) - 20,
                "iso_timestamp": "2026-10-26T11:25:20Z"
            },
            {
                "record_id": "ESP32_E465B8_BOOT1_0004",
                "temperature": 37.2,  # Critical threshold > 35°C
                "humidity": 66.0,
                "timestamp": int(time.time()) - 10,
                "iso_timestamp": "2026-10-26T11:25:30Z"
            }
        ]
    }
    res_batch = client.post("/api/readings/batch", json=batch_payload)
    assert res_batch.status_code == 200, f"Batch insert failed: {res_batch.status_code}"
    b_data = res_batch.get_json()
    assert b_data["inserted_count"] == 3
    assert len(b_data["acknowledged_ids"]) == 3
    print(f"[PASS] POST /api/readings/batch (LittleFS Recovered) -> {b_data['inserted_count']} records synced, IDs acknowledged")

    # 5. Check History Endpoint
    res_hist = client.get("/api/readings/history")
    assert res_hist.status_code == 200
    hist_data = res_hist.get_json()
    assert hist_data["count"] == 4, f"Expected 4 total records, got {hist_data['count']}"
    print(f"[PASS] GET /api/readings/history -> Retrieved {hist_data['count']} chronological readings")

    # 6. Check Recovered LittleFS Endpoint
    res_rec = client.get("/api/readings/recovered")
    assert res_rec.status_code == 200
    rec_data = res_rec.get_json()
    assert rec_data["count"] == 3, f"Expected 3 recovered records, got {rec_data['count']}"
    print(f"[PASS] GET /api/readings/recovered -> Retrieved {rec_data['count']} recovered flash records")

    # 7. Check Latest Reading & Device Status
    res_latest = client.get("/api/readings/latest")
    assert res_latest.status_code == 200
    lat_data = res_latest.get_json()
    assert lat_data["device_status"]["status"] == "ONLINE"
    assert lat_data["device_status"]["total_recovered"] == 3
    print(f"[PASS] GET /api/readings/latest -> Device ONLINE, Total Recovered: {lat_data['device_status']['total_recovered']}")

    # 8. Check Static Dashboard HTML Loading
    res_ui = client.get("/")
    assert res_ui.status_code == 200
    assert b"RELIABLE IoT DATA TRANSMISSION" in res_ui.data
    print(f"[PASS] GET / (Dashboard Index) -> Loaded {len(res_ui.data)} bytes HTML")

    print("==================================================")
    print("ALL 8 INTEGRATION TESTS PASSED WITH 100% SUCCESS!")
    print("==================================================")

if __name__ == "__main__":
    run_tests()
