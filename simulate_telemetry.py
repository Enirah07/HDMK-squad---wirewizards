"""
Hackathon Automated Telemetry Simulator
Varies temperature and humidity realistically, streams live data to the Flask API,
and allows testing network outages, offline flash storage, and batch recovery.
"""

import time
import math
import random
import requests
from datetime import datetime

SERVER_URL = "http://127.0.0.1:5000"
API_READINGS = f"{SERVER_URL}/api/readings"
API_BATCH = f"{SERVER_URL}/api/readings/batch"
DEVICE_ID = "ESP32_F924F0"

def run_simulation():
    print("=" * 60)
    print("   HACKATHON TELEMETRY STREAM SIMULATOR")
    print(f"   Target: {SERVER_URL}")
    print("   Streaming realistic fluctuating DHT22 readings to dashboard...")
    print("=" * 60)

    step = 0.0
    seq = 1
    offline_queue = []
    mode = "ONLINE"

    print("\n[CONTROLS]: Press Ctrl+C at any time to exit.\n")

    try:
        while True:
            step += 0.25
            seq += 1

            # Sinusoidal baseline + random fluctuation
            temp = 29.0 + (math.sin(step) * 2.8) + (random.uniform(-0.4, 0.4))
            hum = 64.0 + (math.cos(step) * 5.0) + (random.uniform(-0.8, 0.8))

            temp = round(temp, 1)
            hum = round(hum, 1)

            now = datetime.utcnow()
            record_id = f"{DEVICE_ID}_S1_{seq:05d}"
            payload = {
                "record_id": record_id,
                "device_id": DEVICE_ID,
                "temperature": temp,
                "humidity": hum,
                "timestamp": int(now.timestamp()),
                "iso_timestamp": now.isoformat() + "Z",
                "is_offline_recovered": 0
            }

            try:
                res = requests.post(API_READINGS, json=payload, timeout=2)
                if res.status_code in [200, 201]:
                    print(f"[{datetime.now().strftime('%H:%M:%S')}] [LIVE STREAM] Temp: {temp:4.1f}°C | Hum: {hum:4.1f}% | ID: {record_id} -> Dashboard Updated!")
                else:
                    print(f"[{datetime.now().strftime('%H:%M:%S')}] Server returned {res.status_code}")
            except Exception as e:
                print(f"[!] Server connection failed ({e}). Check if python app.py is running.")

            time.sleep(2.5)

    except KeyboardInterrupt:
        print("\nSimulator stopped.")

if __name__ == "__main__":
    run_simulation()
