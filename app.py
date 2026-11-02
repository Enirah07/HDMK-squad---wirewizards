"""
Flask REST API Backend for Reliable IoT Data Transmission System
Serves endpoints for ESP32 live streaming, LittleFS batch synchronization,
and real-time web dashboard data visualization.
"""

from flask import Flask, request, jsonify, send_from_directory
from flask_cors import CORS
import os
import time
from datetime import datetime
import database

# Initialize Flask app
FRONTEND_DIR = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "frontend")
app = Flask(__name__, static_folder=FRONTEND_DIR)
CORS(app)  # Enable Cross-Origin requests

# Initialize SQLite database schema
database.init_db()


# ============================================================================
# FRONTEND STATIC ROUTES
# ============================================================================

@app.route("/")
def index():
    """Serves the dashboard index.html."""
    return send_from_directory(FRONTEND_DIR, "index.html")


@app.route("/<path:filename>")
def serve_static(filename):
    """Serves CSS, JS, and asset files."""
    return send_from_directory(FRONTEND_DIR, filename)


# ============================================================================
# ESP32 FIRMWARE COMMUNICATION ENDPOINTS
# ============================================================================

@app.route("/api/health", methods=["GET"])
@app.route("/api/device/health", methods=["GET"])
def device_health():
    """
    Lightweight health-check endpoint.
    Called by ESP32 before transmission to verify active backend connectivity.
    """
    return jsonify({
        "status": "ok",
        "service": "Reliable IoT Backend",
        "server_time_epoch": int(time.time()),
        "server_time_iso": datetime.utcnow().isoformat() + "Z"
    }), 200


@app.route("/api/readings", methods=["POST"])
def receive_reading():
    """
    Receives a single live sensor reading from ESP32.
    Expected JSON:
    {
        "record_id": "ESP32_A1B2_1001",
        "device_id": "ESP32_DEV_1",
        "temperature": 27.5,
        "humidity": 65.0,
        "timestamp": 1729931200,
        "iso_timestamp": "2026-10-26T11:20:00Z",
        "is_offline_recovered": 0
    }
    """
    if not request.is_json:
        return jsonify({"status": "error", "message": "Content-Type must be application/json"}), 400

    payload = request.get_json()

    # Validate essential fields
    required_fields = ["record_id", "temperature", "humidity"]
    for field in required_fields:
        if field not in payload:
            return jsonify({"status": "error", "message": f"Missing required field: '{field}'"}), 422

    # Validate numeric ranges
    try:
        temp = float(payload["temperature"])
        hum = float(payload["humidity"])
    except (ValueError, TypeError):
        return jsonify({"status": "error", "message": "Temperature and humidity must be numeric"}), 422

    inserted, msg = database.insert_reading(payload)

    return jsonify({
        "status": "success" if inserted else "skipped",
        "message": msg,
        "record_id": payload["record_id"],
        "server_time": datetime.utcnow().isoformat() + "Z"
    }), 201 if inserted else 200


@app.route("/api/readings/batch", methods=["POST"])
def receive_batch_readings():
    """
    Receives a batch of recovered offline sensor readings stored in ESP32 LittleFS.
    Expected JSON:
    {
        "device_id": "ESP32_DEV_1",
        "count": 5,
        "readings": [
            { "record_id": "...", "temperature": 32.1, "humidity": 60.5, "timestamp": 1729931210, "iso_timestamp": "..." },
            ...
        ]
    }
    """
    if not request.is_json:
        return jsonify({"status": "error", "message": "Content-Type must be application/json"}), 400

    payload = request.get_json()
    readings = payload.get("readings", [])
    device_id = payload.get("device_id", "ESP32_DEV_1")

    if not isinstance(readings, list) or len(readings) == 0:
        return jsonify({"status": "error", "message": "Expected non-empty 'readings' array"}), 422

    try:
        inserted_count, acked_ids = database.insert_batch_readings(readings, device_id=device_id)
        return jsonify({
            "status": "success",
            "message": f"Synchronized {inserted_count} offline records safely into database",
            "received_count": len(readings),
            "inserted_count": inserted_count,
            "acknowledged_ids": acked_ids
        }), 200
    except Exception as e:
        return jsonify({
            "status": "error",
            "message": f"Batch transaction failed: {str(e)}"
        }), 500


# ============================================================================
# DASHBOARD TELEMETRY & REPORTING ENDPOINTS
# ============================================================================

@app.route("/api/readings/latest", methods=["GET"])
def get_latest_reading():
    """Returns the most recent sensor reading along with live device status."""
    device_id = request.args.get("device_id")
    reading = database.get_latest_reading(device_id)
    status = database.get_device_status(device_id)

    return jsonify({
        "status": "success",
        "reading": reading,
        "device_status": status
    }), 200


@app.route("/api/readings/history", methods=["GET"])
def get_history():
    """Returns chronological sensor readings for Chart.js graphing."""
    limit = int(request.args.get("limit", 100))
    device_id = request.args.get("device_id")
    history = database.get_history(limit=limit, device_id=device_id)
    return jsonify({
        "status": "success",
        "count": len(history),
        "data": history
    }), 200


@app.route("/api/readings/recovered", methods=["GET"])
def get_recovered_readings():
    """Returns records specifically synchronized from LittleFS flash."""
    limit = int(request.args.get("limit", 100))
    recovered = database.get_recovered_readings(limit=limit)
    return jsonify({
        "status": "success",
        "count": len(recovered),
        "data": recovered
    }), 200


@app.route("/api/device/status", methods=["GET"])
def get_device_status():
    """Returns overall device connectivity, health, and outage logs."""
    device_id = request.args.get("device_id")
    status = database.get_device_status(device_id)
    events = database.get_network_events(limit=15)
    return jsonify({
        "status": "success",
        "device_status": status,
        "recent_events": events
    }), 200


@app.route("/api/admin/reset", methods=["POST"])
def admin_reset():
    """Resets database for a fresh demonstration run."""
    database.reset_database()
    return jsonify({
        "status": "success",
        "message": "Database successfully wiped for fresh hackathon demonstration."
    }), 200


if __name__ == "__main__":
    # Host on 0.0.0.0 so ESP32 on the local Wi-Fi network can connect to this machine
    port = int(os.environ.get("PORT", 5000))
    print(f"[*] Starting Reliable IoT Data Transmission Server on http://0.0.0.0:{port}")
    print(f"[*] Access the Dashboard in your browser: http://localhost:{port}")
    app.run(host="0.0.0.0", port=port, debug=False)
