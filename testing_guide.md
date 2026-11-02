# Step-by-Step Implementation & Hackathon Testing Guide
## Reliable IoT Data Transmission During Network Failure

---

## 1. Arduino IDE Setup & Compilation Instructions

### Step 1.1: Install ESP32 Board Core
1. Open **Arduino IDE** (Version 2.x recommended).
2. Navigate to **File > Preferences**.
3. In **Additional boards manager URLs**, add:
   ```text
   https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json
   ```
4. Go to **Tools > Board > Boards Manager...**, search for `esp32`, and install **esp32 by Espressif Systems** (version 2.0.14 or later / 3.x).

### Step 1.2: Install Required Libraries
Open **Tools > Manage Libraries...** (or `Ctrl + Shift + I`) and install:
1. **DHT sensor library** by Adafruit (Version 1.4.6 or later)
   - When prompted to install dependencies (e.g. `Adafruit Unified Sensor`), click **Install All**.

*(Note: `WiFi.h`, `HTTPClient.h`, `LittleFS.h`, and `time.h` are built directly into the ESP32 board package; no external download required!)*

### Step 1.3: Board and Partition Settings
In the **Tools** menu, select:
- **Board:** `DOIT ESP32 DEVKIT V1` (or `ESP32 Dev Module`)
- **Upload Speed:** `921600` (or `115200` if upload fails)
- **CPU Frequency:** `240MHz (WiFi/BT)`
- **Flash Frequency:** `80MHz`
- **Flash Mode:** `QIO`
- **Flash Size:** `4MB (32Mb)`
- **Partition Scheme:** `Default 4MB with spiffs (1.2MB APP / 1.5MB SPIFFS)` or `Minimal SPIFFS (Large APPS with OTA)`
- **Port:** Select your ESP32 COM port (e.g., `COM3`, `COM4`, etc.)

---

## 2. Python Backend & Dashboard Setup

### Step 2.1: Verify Python & Install Dependencies
Open PowerShell or Command Prompt in the project folder:
```powershell
cd c:\Users\Murugesan\Desktop\HDMK\Reliable-IoT-Data-Transmission\backend
python -m pip install -r requirements.txt
```

### Step 2.2: Launch the Backend
```powershell
python app.py
```
Expected output:
```text
[*] Starting Reliable IoT Data Transmission Server on http://0.0.0.0:5000
[*] Access the Dashboard in your browser: http://localhost:5000
 * Running on http://127.0.0.1:5000
 * Running on http://172.16.204.209:5000
```

### Step 2.3: Access the Web Dashboard
Open your web browser and navigate to:
```text
http://localhost:5000
```
(Or on any smartphone/laptop on the same Wi-Fi: `http://172.16.204.209:5000`)

---

## 3. Phase-by-Phase Implementation Plan

### Phase 1 — Hardware Setup
- **Objective:** Connect ESP32 DevKit V1, DHT22 sensor, and active buzzer driver on breadboard.
- **Verification:** Double-check DHT22 Pin 2 to GPIO 4 (with 10k pull-up if bare), Buzzer driver to GPIO 18, and VCC to 3.3V.

### Phase 2 — Sensor Sampling & Validation
- **Objective:** Verify reading real ambient temperature and humidity every 10 seconds.
- **Verification:** Open Arduino Serial Monitor at **115200 baud**.
- **Expected Output:**
  ```text
  [SENSOR] Record ID: ESP32_E465B8_S1_00001 | Temp: 28.50 °C | Humidity: 62.10 % | Time: 2026-10-26T11:20:00Z
  ```

### Phase 3 — LittleFS Flash Persistence Testing
- **Objective:** Verify records are saved in non-volatile flash and survive micro-controller power cycles.
- **Testing Method:**
  1. Boot ESP32 with Wi-Fi disabled in code or router off.
  2. Let it record 3 offline samples.
  3. Press ESP32 **EN / Reset** button.
  4. Note that upon reboot, Serial Monitor logs:
     ```text
     [LittleFS] Boot Session ID: 2 | Flash Total: 1408 KB | Used: 12 KB
     [SYSTEM] Pending Offline Records in LittleFS: 3
     ```
  5. Records remain intact and counter safely increments!

### Phase 4 — Wi-Fi & Backend Reachability Check
- **Objective:** Verify two-tier network check (Wi-Fi physical link + HTTP health ping).
- **Testing Method:** 
  - If Wi-Fi is connected but Flask server is stopped, ESP32 detects `serverReachable == false` and routes to OFFLINE mode.
  - If Flask server starts, ESP32 immediately detects `serverReachable == true` and switches to ONLINE mode.

### Phase 5 & 6 — Backend & Dashboard Integration
- **Objective:** Confirm real-time WebSocket/HTTP polling visualizes telemetry without manual browser refresh.
- **Verification:** Dashboard displays pulsing green `ONLINE` badge, live temperature/humidity gauges, and real-time Chart.js lines.

### Phase 7 — Normal Online Data Transmission
- **Objective:** Stream real DHT22 sensor readings every 10s to Flask API and store in SQLite.
- **Verification:** SQLite database increments `total_readings`, buzzer stays **OFF**, and threshold alert checking is **DISABLED**.

### Phase 8 — Offline Data Storage (Simulate Network Drop)
- **Objective:** Disconnect Wi-Fi (turn off router/mobile hotspot or disconnect laptop from network).
- **Verification:**
  - ESP32 Serial Monitor prints:
    ```text
    [EVENT] >>> NETWORK OR SERVER FAILURE! SWITCHING TO OFFLINE MODE <<<
    [LittleFS] Stored record locally in flash: ESP32_E465B8_S1_00012 (Queue size: 1)
    ```
  - Dashboard detects timeout after 25s and switches status badge to glowing red `OFFLINE`.

### Phase 9 — Offline Temperature Alert Verification
- **Objective:** Sensed temperature rises above 35°C during network outage.
- **Testing Method:**
  - While offline, gently warm the DHT22 sensor (e.g. breath warmth, warm mug, or warm hand) above 35.0°C.
  - **Result:**
    - Serial Monitor prints:
      ```text
      [ALERT-EVAL] OFFLINE Mode Active. Current Temp: 36.40 °C | Threshold: 35.00 °C
      [BUZZER] >>> OFFLINE TEMPERATURE ALERT TRIGGERED (> 35°C)! ACTIVATING BUZZER <<<
      ```
    - Buzzer sounds acoustic alarm!
    - Storing in LittleFS continues without interruption!
  - When temperature cools below 34.0°C (hysteresis), buzzer silences automatically.

### Phase 10 — Automatic Reconnection & Data Synchronization
- **Objective:** Restore Wi-Fi / backend server.
- **Verification:**
  - Buzzer is silenced instantly upon reconnection!
  - ESP32 automatically uploads all accumulated LittleFS records in batches to `/api/readings/batch`.
  - Flask confirms insertion into SQLite with HTTP 200/201.
  - Flash pending queue is cleared only after server acknowledgement!
  - Dashboard recovers the data and renders points with vibrant purple diamond markers on Chart.js and updates the Recovered Records Table!

### Phase 11 — Complete Integration Verification
- Run the full end-to-end automated backend test suite:
  ```powershell
  python backend/test_api.py
  ```

---

## 4. Live Hackathon Demonstration Guide (Judge Presentation)

### Demo Script & Scenarios:

#### Scenario 1: Normal Operation (Online Baseline)
1. Turn on ESP32 connected to the laptop hotspot.
2. Show judges the Web Dashboard on your laptop screen:
   - Green indicator: `ONLINE (ESP32 LINKED)`.
   - Real-time DHT22 temperature (e.g. 28.5°C) and humidity (e.g. 62%).
   - Buzzer status card: `QUIET (ONLINE MODE)`.
   - Explain to judges: *"During normal online operation, sensor data is transmitted directly to the cloud/server every 10 seconds. Local buzzer alerts are intentionally disabled in online mode."*

#### Scenario 2: Network Outage Simulation
1. Turn off your Wi-Fi hotspot or terminate the Flask server with `Ctrl+C`.
2. Point out ESP32 Serial Monitor:
   - ESP32 detects network drop: `[MODE] >>> OFFLINE OPERATION <<<`.
   - Each 10-second reading is saved to LittleFS internal flash: `[LittleFS] Stored record locally in flash (Queue size: 1, 2, 3...)`.
3. Point out Dashboard:
   - Shows `OFFLINE (NETWORK DROPPED)`.
   - Explain: *"Notice no data is lost; every single sensor reading with its original hardware timestamp is safely logged in ESP32 flash memory."*

#### Scenario 3: Offline Emergency Alert
1. While still offline, breathe warm air on the DHT22 sensor or hold your fingers over it to raise the temperature past 35.0°C.
2. **Immediate Result:**
   - Active buzzer starts buzzing loudly!
   - Serial Monitor prints: `[BUZZER] >>> OFFLINE TEMPERATURE ALERT TRIGGERED (> 35°C)! ACTIVATING BUZZER <<<`.
   - Explain: *"When the network fails, the node activates an offline fail-safe emergency alert. The buzzer warns personnel locally because remote notifications cannot be sent over the dead network."*

#### Scenario 4: Reconnection & Automatic Recovery
1. Turn the Wi-Fi hotspot back on (or restart `python app.py`).
2. **Immediate Result:**
   - Buzzer stops buzzing immediately upon reconnection!
   - ESP32 logs: `[SYNC] >>> AUTOMATIC DATA RECOVERY STARTED <<<`.
   - All buffered readings are uploaded to `/api/readings/batch`.
   - Server acknowledges the record IDs; ESP32 clears the flash queue.

#### Scenario 5: Data Integrity Verification
1. Direct the judges' attention to the Web Dashboard:
   - Status badge returns to `ONLINE`.
   - Recovered Records Table displays all records captured during the blackout, with their **exact original timestamps**!
   - Chart.js plots the recovered points with distinctive purple diamond icons (`◆ LittleFS Recovered`).
   - Click the **"📥 Export CSV"** button and open the spreadsheet to show zero missing records, zero duplicate IDs, and complete timeline continuity!

---

## 5. Judge Demonstration Checklist

- [ ] **Hardware Authenticity:** DHT22 reading live temperature and humidity (not simulated or randomized).
- [ ] **No SD Card Used:** Internal ESP32 LittleFS flash partition utilized.
- [ ] **Dual Network Check:** Firmware validates physical Wi-Fi status and HTTP health endpoint reachability.
- [ ] **Online Silence:** Buzzer remains completely silent during online operation even if sensor warms up.
- [ ] **Offline Storage:** Sensor continues sampling every 10 seconds and storing in LittleFS during network outage.
- [ ] **Offline Alert:** Buzzer triggers when temperature > 35°C strictly during offline mode.
- [ ] **Hysteresis:** Buzzer turns on at 35°C and deactivates at 34°C to prevent acoustic chatter.
- [ ] **Auto-Recovery:** Automatic batch synchronization when Wi-Fi returns.
- [ ] **Safe Acknowledgement:** Stored records are only removed after HTTP 200 server acknowledgement.
- [ ] **Duplicate Prevention:** SQLite table enforces `UNIQUE(record_id)` with `INSERT OR IGNORE`.
- [ ] **Visual Distinction:** Dashboard visually marks recovered readings distinctively from live stream.
