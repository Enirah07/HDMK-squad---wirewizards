/**
 * ============================================================================
 * PROJECT: Reliable IoT Data Transmission During Network Failure
 * TARGET:  ESP32 DevKit V1 (30-pin or 36-pin)
 * AUTHOR:  Expert IoT & Embedded Systems Engineering Team
 * 
 * DESCRIPTION:
 * Continuously samples temperature & humidity every 10 seconds.
 * 
 * FEATURES:
 * - Built-in zero-dependency DHT driver: Supports BOTH DHT11 (blue) and DHT22 (white)!
 * - No external DHT libraries required to compile.
 * - Non-volatile LittleFS flash storage during network outages.
 * - Offline-only emergency buzzer threshold alert (> 35°C with hysteresis).
 * - Automatic reconnection detection and batch data synchronization.
 * ============================================================================
 */

#include <WiFi.h>
#include <HTTPClient.h>
#include <LittleFS.h>
#include <time.h>

// ============================================================================
// SENSOR SELECTION & HARDWARE PIN CONFIGURATION
// ============================================================================
// Sensor Model Options:
enum SensorModel { MODEL_DHT11, MODEL_DHT22 };

// >>> SELECT YOUR SENSOR HERE <<<
// Set to MODEL_DHT22 for white sensor, or MODEL_DHT11 for blue sensor
const SensorModel SELECTED_SENSOR = MODEL_DHT22;

#define DHT_PIN         4         // DHT Data pin connected to GPIO 4
#define BUZZER_PIN      18        // Active Buzzer / Transistor Base on GPIO 18
#define STATUS_LED_PIN  2         // Onboard Blue LED on DevKit V1

// ============================================================================
// NETWORK & BACKEND CONFIGURATION
// ============================================================================
// Your Wi-Fi credentials
const char* WIFI_SSID     = "TestIoT";
const char* WIFI_PASSWORD = "password123";

// Backend Server IP & Port (Update with your laptop's local IPv4)
const char* SERVER_HOST   = "10.178.120.40";
const int   SERVER_PORT   = 5000;

// API Endpoints
const char* API_HEALTH   = "/api/health";
const char* API_READINGS = "/api/readings";
const char* API_BATCH    = "/api/readings/batch";

// ============================================================================
// SYSTEM PARAMETERS & THRESHOLDS
// ============================================================================
const unsigned long SAMPLE_INTERVAL_MS   = 10000;   // Sensor sampling every 10 seconds
const unsigned long SERVER_TIMEOUT_MS    = 2500;    // HTTP timeout in milliseconds
const unsigned long RECONNECT_INTERVAL   = 5000;    // Network reconnect check interval
const unsigned long BATCH_SYNC_INTERVAL  = 12000;   // Minimum gap between batch syncs

// Alert Policy (OFFLINE ONLY)
const float TEMP_THRESHOLD_HIGH          = 35.0;    // Turn ON buzzer if temp >= 35.0°C
const float TEMP_THRESHOLD_LOW           = 34.0;    // Turn OFF buzzer if temp <= 34.0°C (Hysteresis)

// LittleFS Storage File Paths
const char* PENDING_DATA_PATH            = "/offline_pending.jsonl";
const char* TEMP_SWAP_PATH               = "/offline_swap.jsonl";
const char* BOOT_COUNTER_PATH            = "/boot_session.txt";
const size_t MAX_OFFLINE_RECORDS         = 15000;   // Safeguard limit (~1.2 MB flash)

// ============================================================================
// ZERO-DEPENDENCY BUILT-IN DHT DRIVER (DHT11 & DHT22 / AM2302)
// Eliminates all external library compilation and version conflicts!
// ============================================================================
class DHTSensorDriver {
public:
    DHTSensorDriver(uint8_t pin, SensorModel model) : _pin(pin), _model(model) {}

    void begin() {
        pinMode(_pin, INPUT_PULLUP);
    }

    bool read(float &temperature, float &humidity) {
        uint8_t data[5] = {0, 0, 0, 0, 0};

        // Send start pulse
        pinMode(_pin, OUTPUT);
        digitalWrite(_pin, LOW);
        if (_model == MODEL_DHT11) {
            delay(20); // DHT11 needs at least 18ms
        } else {
            delay(4);  // DHT22 needs 1-10ms
        }
        digitalWrite(_pin, HIGH);
        delayMicroseconds(35);
        pinMode(_pin, INPUT_PULLUP);

        // Wait for sensor response: LOW ~80us, HIGH ~80us
        if (!waitState(LOW, 120)) return false;
        if (!waitState(HIGH, 120)) return false;
        if (!waitState(LOW, 120)) return false;

        // Read 40 data bits (5 bytes)
        for (int i = 0; i < 40; i++) {
            if (!waitState(HIGH, 80)) return false;

            unsigned long start = micros();
            if (!waitState(LOW, 100)) return false;

            // If pulse duration > 40 microseconds, bit is '1'; otherwise '0'
            if ((micros() - start) > 40) {
                data[i / 8] |= (1 << (7 - (i % 8)));
            }
        }

        // Verify checksum: Byte 4 == (Byte 0 + Byte 1 + Byte 2 + Byte 3) & 0xFF
        uint8_t checksum = (data[0] + data[1] + data[2] + data[3]) & 0xFF;
        if (data[4] != checksum) {
            return false;
        }

        // Decode data according to selected model
        if (_model == MODEL_DHT11) {
            humidity = (float)data[0] + ((float)data[1] * 0.1f);
            temperature = (float)data[2] + ((float)data[3] * 0.1f);
        } else { // DHT22 / AM2302
            humidity = (float)(((uint16_t)data[0] << 8) | data[1]) * 0.1f;
            int16_t rawTemp = (int16_t)(((uint16_t)(data[2] & 0x7F) << 8) | data[3]);
            temperature = (float)rawTemp * 0.1f;
            if (data[2] & 0x80) {
                temperature = -temperature;
            }
        }

        // Sanity range check
        if (humidity < 0.0 || humidity > 100.0) return false;
        if (temperature < -40.0 || temperature > 85.0) return false;

        return true;
    }

private:
    uint8_t _pin;
    SensorModel _model;

    bool waitState(uint8_t state, unsigned long timeoutUs) {
        unsigned long start = micros();
        while (digitalRead(_pin) != state) {
            if (micros() - start > timeoutUs) return false;
        }
        return true;
    }
};

// ============================================================================
// GLOBAL INSTANCES & SYSTEM STATE
// ============================================================================
DHTSensorDriver dht(DHT_PIN, SELECTED_SENSOR);

String deviceMacAddress   = "";
String deviceId           = "ESP32_NODE_1";
uint32_t bootSessionId    = 1;
uint32_t recordSequence   = 0;

bool isOnline             = false;
bool buzzerActive         = false;
bool ntpSynchronized      = false;
unsigned long lastSampleTime = 0;
unsigned long lastReconnectCheck = 0;
unsigned long lastSyncAttempt = 0;

// NTP Time Server Configuration
const char* NTP_SERVER_1  = "pool.ntp.org";
const char* NTP_SERVER_2  = "time.nist.gov";
const long  GMT_OFFSET_SEC = 19800;                 // Indian Standard Time (UTC +5:30) = 19800s
const int   DAYLIGHT_OFFSET_SEC = 0;

// ============================================================================
// FORWARD DECLARATIONS
// ============================================================================
void initHardware();
void initStorage();
void connectWiFi();
void syncNtpTime();
bool checkServerConnectivity();
void performSensorCycle();
bool readSensor(float &temp, float &hum);
String generateRecordId();
time_t getCurrentTimestamp(String &isoString);
bool transmitLiveReading(const String &recId, float temp, float hum, time_t ts, const String &isoTime);
void saveOfflineRecord(const String &recId, float temp, float hum, time_t ts, const String &isoTime);
void evaluateOfflineThreshold(float temp);
void setBuzzerState(bool active);
void synchronizeOfflineData();
bool uploadBatchToBackend(String records[], int count);
size_t countPendingRecords();

// ============================================================================
// SETUP FUNCTION
// ============================================================================
void setup() {
    Serial.begin(115200);
    delay(1000);

    Serial.println("\n========================================================");
    Serial.println("   RELIABLE IoT DATA TRANSMISSION DURING NETWORK FAILURE");
    Serial.println("   ESP32 DevKit V1 Firmware | Fault-Tolerant Node");
    Serial.printf ("   Active Sensor Model: %s on GPIO %d\n", 
                   (SELECTED_SENSOR == MODEL_DHT11) ? "DHT11 (Blue)" : "DHT22 (White)", DHT_PIN);
    Serial.println("========================================================");

    initHardware();
    initStorage();

    // Derive unique Device ID from ESP32 hardware Efuse MAC
    WiFi.mode(WIFI_STA);
    uint64_t chipId = ESP.getEfuseMac();
    char macBuf[18];
    snprintf(macBuf, sizeof(macBuf), "%04X%08X", (uint16_t)(chipId >> 32), (uint32_t)chipId);
    deviceMacAddress = String(macBuf);
    deviceId = "ESP32_" + deviceMacAddress.substring(deviceMacAddress.length() - 6);
    Serial.printf("[SYSTEM] Initialized Device ID: %s (Chip ID: %s)\n", deviceId.c_str(), deviceMacAddress.c_str());

    // Connect to WiFi
    connectWiFi();

    // Initialize NTP time sync if connected
    if (WiFi.status() == WL_CONNECTED) {
        syncNtpTime();
    }

    // Initial server availability check
    isOnline = checkServerConnectivity();
    Serial.printf("[SYSTEM] Initial Network State: %s\n", isOnline ? "ONLINE" : "OFFLINE");
    Serial.printf("[SYSTEM] Pending Offline Records in LittleFS: %d\n", countPendingRecords());
    Serial.println("========================================================\n");
}

// ============================================================================
// MAIN LOOP
// ============================================================================
void loop() {
    unsigned long currentMillis = millis();

    // 1. Maintain Wi-Fi Connection
    if (WiFi.status() != WL_CONNECTED) {
        if (currentMillis - lastReconnectCheck >= RECONNECT_INTERVAL) {
            lastReconnectCheck = currentMillis;
            Serial.printf("[WIFI] Reconnecting to '%s'...\n", WIFI_SSID);
            WiFi.disconnect();
            WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
        }
    }

    // 2. Scheduled Sensor Sampling Cycle (Every 10 seconds)
    if (currentMillis - lastSampleTime >= SAMPLE_INTERVAL_MS) {
        lastSampleTime = currentMillis;
        performSensorCycle();
    }

    // 3. Process Pending Offline Synchronization (When Online)
    if (isOnline && (currentMillis - lastSyncAttempt >= BATCH_SYNC_INTERVAL)) {
        lastSyncAttempt = currentMillis;
        if (countPendingRecords() > 0) {
            synchronizeOfflineData();
        }
    }

    // Yield for background RTOS tasks
    vTaskDelay(10 / portTICK_PERIOD_MS);
}

// ============================================================================
// SENSOR CYCLE & OPERATIONAL STATE ROUTING
// ============================================================================
void performSensorCycle() {
    float temperature = 0.0;
    float humidity = 0.0;

    Serial.println("\n--- [START SENSOR CYCLE] ---");

    // STEP 1: SENSOR DATA COLLECTION
    if (!readSensor(temperature, humidity)) {
        Serial.println("[SENSOR] Read failed or checksum error. Will retry in next cycle.");
        return;
    }

    recordSequence++;
    String recordId = generateRecordId();
    String isoTimestamp = "";
    time_t timestamp = getCurrentTimestamp(isoTimestamp);

    Serial.printf("[SENSOR] Record ID: %s | Temp: %.2f °C | Humidity: %.2f %% | Time: %s\n",
                  recordId.c_str(), temperature, humidity, isoTimestamp.c_str());

    // STEP 2: NETWORK STATUS CHECK
    // Verifies BOTH Wi-Fi physical link AND Backend HTTP Server response
    bool serverReachable = checkServerConnectivity();

    // Detect state transitions
    if (!isOnline && serverReachable) {
        Serial.println("\n[EVENT] >>> NETWORK RESTORED! TRANSITIONING TO ONLINE MODE <<<");
        isOnline = true;
        setBuzzerState(false); // Force buzzer OFF immediately upon reconnect
        synchronizeOfflineData();
    } else if (isOnline && !serverReachable) {
        Serial.println("\n[EVENT] >>> NETWORK OR SERVER FAILURE! SWITCHING TO OFFLINE MODE <<<");
        isOnline = false;
    }

    // STEP 3: ONLINE OPERATION
    if (isOnline) {
        Serial.println("[MODE] >>> ONLINE OPERATION <<<");
        
        // RULE: Buzzer must be forced OFF during online operation
        setBuzzerState(false);

        // RULE: Temperature threshold alert check is DISABLED in ONLINE mode
        Serial.println("[ALERT] Online Mode active: Temperature threshold buzzer check is DISABLED.");

        // Transmit live reading to Flask backend
        bool sent = transmitLiveReading(recordId, temperature, humidity, timestamp, isoTimestamp);
        if (!sent) {
            Serial.println("[ONLINE] Live upload failed. Buffering record to LittleFS.");
            saveOfflineRecord(recordId, temperature, humidity, timestamp, isoTimestamp);
            isOnline = false; // Degrade to offline mode
        }
    } 
    // STEP 4: OFFLINE OPERATION
    else {
        Serial.println("[MODE] >>> OFFLINE OPERATION <<<");

        // Save sensor reading to LittleFS internal flash
        saveOfflineRecord(recordId, temperature, humidity, timestamp, isoTimestamp);

        // STEP 5 & 6: TEMPERATURE THRESHOLD CHECK & BUZZER ALERT (OFFLINE ONLY)
        evaluateOfflineThreshold(temperature);
    }

    Serial.println("--- [END SENSOR CYCLE] ---\n");
}

// ============================================================================
// HARDWARE INITIALIZATION
// ============================================================================
void initHardware() {
    pinMode(BUZZER_PIN, OUTPUT);
    digitalWrite(BUZZER_PIN, LOW); // Default OFF
    buzzerActive = false;

    pinMode(STATUS_LED_PIN, OUTPUT);
    digitalWrite(STATUS_LED_PIN, LOW);

    dht.begin();
    Serial.printf("[HARDWARE] DHT driver initialized on GPIO %d.\n", DHT_PIN);
    Serial.printf("[HARDWARE] Buzzer driver initialized on GPIO %d.\n", BUZZER_PIN);
}

// ============================================================================
// LITTLEFS INITIALIZATION & STORAGE HELPERS
// ============================================================================
void initStorage() {
    if (!LittleFS.begin(true)) {
        Serial.println("[LittleFS] Critical Error: Failed to mount LittleFS filesystem.");
        return;
    }
    Serial.println("[LittleFS] Filesystem mounted successfully.");

    // Manage boot session counter to guarantee global ID uniqueness across reboots
    if (LittleFS.exists(BOOT_COUNTER_PATH)) {
        File bf = LittleFS.open(BOOT_COUNTER_PATH, "r");
        if (bf) {
            bootSessionId = bf.parseInt() + 1;
            bf.close();
        }
    }
    File bfWrite = LittleFS.open(BOOT_COUNTER_PATH, "w");
    if (bfWrite) {
        bfWrite.println(bootSessionId);
        bfWrite.close();
    }
    Serial.printf("[LittleFS] Boot Session ID: %u | Flash Total: %u KB | Used: %u KB\n",
                  bootSessionId,
                  (unsigned int)(LittleFS.totalBytes() / 1024),
                  (unsigned int)(LittleFS.usedBytes() / 1024));
}

// ============================================================================
// WI-FI & TIME MANAGEMENT
// ============================================================================
void connectWiFi() {
    Serial.println("\n--------------------------------------------------------");
    Serial.printf("[WIFI] Target SSID: '%s'\n", WIFI_SSID);
    Serial.println("[WIFI] Scanning 2.4 GHz airwaves for visible networks...");

    WiFi.disconnect(true);
    delay(200);
    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false); // Disable power saving for maximum RF stability

    // Scan available networks
    int n = WiFi.scanNetworks();
    bool ssidFound = false;
    Serial.printf("[WIFI] Scan finished. Found %d networks:\n", n);
    for (int i = 0; i < n; ++i) {
        String foundSsid = WiFi.SSID(i);
        int rssi = WiFi.RSSI(i);
        Serial.printf("  [%d] '%s' (Signal: %d dBm)\n", i + 1, foundSsid.c_str(), rssi);
        if (foundSsid == String(WIFI_SSID)) {
            ssidFound = true;
        }
    }

    if (!ssidFound) {
        Serial.println("\n[!] WARNING: Target SSID was NOT detected in the 2.4 GHz scan!");
        Serial.println("[!] REASON: The phone hotspot is broadcasting on 5 GHz only, or hotspot is OFF.");
        Serial.println("[!] FIX: On the phone, go to Hotspot Settings and change 'AP Band' to 2.4 GHz.");
        Serial.println("--------------------------------------------------------\n");
    } else {
        Serial.printf("\n[+] Target SSID '%s' FOUND on 2.4 GHz! Connecting...\n", WIFI_SSID);
    }

    WiFi.setAutoReconnect(true);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 30) {
        delay(500);
        Serial.print(".");
        attempts++;
    }

    if (WiFi.status() == WL_CONNECTED) {
        Serial.println("\n[WIFI] Connected successfully!");
        Serial.printf("[WIFI] ESP32 IP Address: %s\n", WiFi.localIP().toString().c_str());
        Serial.printf("[WIFI] Signal Strength (RSSI): %d dBm\n", WiFi.RSSI());
    } else {
        Serial.printf("\n[WIFI] Connection timed out (Status: %d). Operating in OFFLINE mode.\n", WiFi.status());
    }
    Serial.println("--------------------------------------------------------\n");
}

void syncNtpTime() {
    Serial.println("[NTP] Synchronizing internal RTC with NTP servers...");
    configTime(GMT_OFFSET_SEC, DAYLIGHT_OFFSET_SEC, NTP_SERVER_1, NTP_SERVER_2);

    struct tm timeinfo;
    if (getLocalTime(&timeinfo, 5000)) {
        ntpSynchronized = true;
        char buffer[32];
        strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &timeinfo);
        Serial.printf("[NTP] Accurate RTC Time Acquired: %s\n", buffer);
    } else {
        Serial.println("[NTP] NTP sync failed. Fallback to monotonic time reference.");
    }
}

time_t getCurrentTimestamp(String &isoString) {
    time_t now;
    time(&now);

    // If NTP was synced or time is past year 2024
    if (now > 1700000000) {
        struct tm timeinfo;
        gmtime_r(&now, &timeinfo);
        char buf[30];
        strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &timeinfo);
        isoString = String(buf);
        return now;
    } else {
        // Fallback timestamp based on millis uptime since boot
        unsigned long uptimeSec = millis() / 1000;
        time_t syntheticTime = 1729900000 + uptimeSec; // Known reference base
        struct tm timeinfo;
        gmtime_r(&syntheticTime, &timeinfo);
        char buf[30];
        strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &timeinfo);
        isoString = String(buf);
        return syntheticTime;
    }
}

// ============================================================================
// SENSOR READING & VALIDATION
// ============================================================================
bool readSensor(float &temp, float &hum) {
    if (dht.read(temp, hum)) {
        return true;
    }

    // Quick retry after 250ms
    delay(250);
    return dht.read(temp, hum);
}

String generateRecordId() {
    char idBuffer[48];
    snprintf(idBuffer, sizeof(idBuffer), "%s_S%u_%05u", deviceId.c_str(), bootSessionId, recordSequence);
    return String(idBuffer);
}

// ============================================================================
// NETWORK CONNECTIVITY CHECK (WI-FI + BACKEND REACHABILITY)
// ============================================================================
bool checkServerConnectivity() {
    if (WiFi.status() != WL_CONNECTED) {
        Serial.printf("[NET-CHECK] Wi-Fi is disconnected (Status: %d). Operating OFFLINE.\n", WiFi.status());
        return false;
    }

    HTTPClient http;
    http.setTimeout(SERVER_TIMEOUT_MS);

    String healthUrl = "http://" + String(SERVER_HOST) + ":" + String(SERVER_PORT) + API_HEALTH;
    if (!http.begin(healthUrl)) {
        Serial.println("[NET-CHECK] Failed to begin HTTP client connection.");
        return false;
    }

    int httpCode = http.GET();
    http.end();

    if (httpCode == 200) {
        return true;
    } else {
        Serial.printf("[NET-CHECK] Wi-Fi is OK, but Backend Server at %s:%d returned HTTP %d (Expected 200). Operating OFFLINE.\n",
                      SERVER_HOST, SERVER_PORT, httpCode);
        return false;
    }
}

// ============================================================================
// LIVE TRANSMISSION (ONLINE MODE)
// ============================================================================
bool transmitLiveReading(const String &recId, float temp, float hum, time_t ts, const String &isoTime) {
    HTTPClient http;
    http.setTimeout(SERVER_TIMEOUT_MS);

    String url = "http://" + String(SERVER_HOST) + ":" + String(SERVER_PORT) + API_READINGS;
    if (!http.begin(url)) {
        return false;
    }

    http.addHeader("Content-Type", "application/json");

    // Construct JSON Payload
    char jsonPayload[256];
    snprintf(jsonPayload, sizeof(jsonPayload),
        "{\"record_id\":\"%s\",\"device_id\":\"%s\",\"temperature\":%.2f,\"humidity\":%.2f,\"timestamp\":%ld,\"iso_timestamp\":\"%s\",\"is_offline_recovered\":0}",
        recId.c_str(), deviceId.c_str(), temp, hum, (long)ts, isoTime.c_str()
    );

    int httpCode = http.POST(jsonPayload);
    String response = http.getString();
    http.end();

    if (httpCode == 200 || httpCode == 201) {
        Serial.printf("[ONLINE] Live upload SUCCESS (HTTP %d): %s\n", httpCode, response.c_str());
        return true;
    } else {
        Serial.printf("[ONLINE] Live upload FAILED (HTTP %d)\n", httpCode);
        return false;
    }
}

// ============================================================================
// OFFLINE STORAGE IN LittleFS FLASH
// ============================================================================
void saveOfflineRecord(const String &recId, float temp, float hum, time_t ts, const String &isoTime) {
    size_t pendingCount = countPendingRecords();
    if (pendingCount >= MAX_OFFLINE_RECORDS) {
        Serial.println("[LittleFS] Warning: Max offline storage limit reached! Flash protected.");
        return;
    }

    File file = LittleFS.open(PENDING_DATA_PATH, "a");
    if (!file) {
        Serial.println("[LittleFS] Error: Failed to open file for appending offline data.");
        return;
    }

    // Save line as compact JSON
    char lineBuffer[256];
    snprintf(lineBuffer, sizeof(lineBuffer),
        "{\"record_id\":\"%s\",\"device_id\":\"%s\",\"temperature\":%.2f,\"humidity\":%.2f,\"timestamp\":%ld,\"iso_timestamp\":\"%s\"}\n",
        recId.c_str(), deviceId.c_str(), temp, hum, (long)ts, isoTime.c_str()
    );

    file.print(lineBuffer);
    file.close();

    Serial.printf("[LittleFS] Stored record locally in flash: %s (Queue size: %d)\n", recId.c_str(), pendingCount + 1);
}

size_t countPendingRecords() {
    if (!LittleFS.exists(PENDING_DATA_PATH)) {
        return 0;
    }

    File file = LittleFS.open(PENDING_DATA_PATH, "r");
    if (!file) return 0;

    size_t count = 0;
    while (file.available()) {
        String line = file.readStringUntil('\n');
        line.trim();
        if (line.length() > 0) count++;
    }
    file.close();
    return count;
}

// ============================================================================
// OFFLINE TEMPERATURE THRESHOLD EVALUATION & BUZZER
// ============================================================================
void evaluateOfflineThreshold(float temp) {
    // Strictly called ONLY when OFFLINE. Online mode completely bypasses this logic.

    Serial.printf("[ALERT-EVAL] OFFLINE Mode Active. Current Temp: %.2f °C | Threshold: %.2f °C\n",
                  temp, TEMP_THRESHOLD_HIGH);

    // Hysteresis Logic:
    // If temp >= 35.0°C -> Turn Buzzer ON
    // If temp <= 34.0°C -> Turn Buzzer OFF
    // Between 34.0 and 35.0°C -> Retain previous state
    if (temp >= TEMP_THRESHOLD_HIGH) {
        if (!buzzerActive) {
            Serial.println("[BUZZER] >>> OFFLINE TEMPERATURE ALERT TRIGGERED (> 35°C)! ACTIVATING BUZZER <<<");
            setBuzzerState(true);
        } else {
            Serial.println("[BUZZER] Offline alert remains ACTIVE.");
        }
    } else if (temp <= TEMP_THRESHOLD_LOW) {
        if (buzzerActive) {
            Serial.println("[BUZZER] Temperature dropped below safe threshold (<= 34°C). Deactivating buzzer.");
            setBuzzerState(false);
        } else {
            Serial.println("[BUZZER] Offline temperature is SAFE. Buzzer remains OFF.");
        }
    } else {
        Serial.printf("[BUZZER] In hysteresis deadband (34-35°C). Buzzer remains: %s\n",
                      buzzerActive ? "ON" : "OFF");
    }
}

void setBuzzerState(bool active) {
    buzzerActive = active;
    digitalWrite(BUZZER_PIN, active ? HIGH : LOW);
    digitalWrite(STATUS_LED_PIN, active ? HIGH : LOW);
}

// ============================================================================
// AUTOMATIC DATA SYNCHRONIZATION (RECONNECTION RECOVERY)
// ============================================================================
void synchronizeOfflineData() {
    if (!LittleFS.exists(PENDING_DATA_PATH)) {
        return;
    }

    size_t pendingCount = countPendingRecords();
    if (pendingCount == 0) {
        return;
    }

    Serial.println("\n========================================================");
    Serial.printf("[SYNC] >>> AUTOMATIC DATA RECOVERY STARTED: %d RECORDS PENDING <<<\n", pendingCount);
    Serial.println("========================================================");

    File sourceFile = LittleFS.open(PENDING_DATA_PATH, "r");
    if (!sourceFile) {
        Serial.println("[SYNC] Error: Unable to open pending file for reading.");
        return;
    }

    // Read in batches of up to 10 records
    const int BATCH_SIZE = 10;
    String recordsBatch[BATCH_SIZE];
    int currentBatchCount = 0;
    bool syncError = false;

    // Temporary storage for records that fail or remain unacknowledged
    File swapFile = LittleFS.open(TEMP_SWAP_PATH, "w");
    if (!swapFile) {
        Serial.println("[SYNC] Error: Unable to create temporary swap file.");
        sourceFile.close();
        return;
    }

    while (sourceFile.available()) {
        String line = sourceFile.readStringUntil('\n');
        line.trim();
        if (line.length() == 0) continue;

        recordsBatch[currentBatchCount++] = line;

        // When batch is full, upload it
        if (currentBatchCount >= BATCH_SIZE) {
            bool batchSuccess = uploadBatchToBackend(recordsBatch, currentBatchCount);
            if (!batchSuccess) {
                // If transmission fails, write back to swap file to retain records!
                for (int i = 0; i < currentBatchCount; i++) {
                    swapFile.println(recordsBatch[i]);
                }
                syncError = true;
                break;
            }
            currentBatchCount = 0;
        }
    }

    // Upload any remaining records in final partial batch
    if (currentBatchCount > 0 && !syncError) {
        bool batchSuccess = uploadBatchToBackend(recordsBatch, currentBatchCount);
        if (!batchSuccess) {
            for (int i = 0; i < currentBatchCount; i++) {
                swapFile.println(recordsBatch[i]);
            }
            syncError = true;
        }
    }

    // If interrupted midway, write remaining unprocessed lines from source to swap
    if (syncError) {
        while (sourceFile.available()) {
            String remainingLine = sourceFile.readStringUntil('\n');
            swapFile.println(remainingLine);
        }
    }

    sourceFile.close();
    swapFile.close();

    // Replace pending file with swap file (atomic cleanup)
    LittleFS.remove(PENDING_DATA_PATH);
    if (syncError) {
        LittleFS.rename(TEMP_SWAP_PATH, PENDING_DATA_PATH);
        Serial.println("[SYNC] Synchronization interrupted. Unsent records safely retained in LittleFS.");
    } else {
        LittleFS.remove(TEMP_SWAP_PATH);
        Serial.println("[SYNC] >>> ALL PENDING RECORDS SUCCESSFULLY SYNCHRONIZED AND ACKNOWLEDGED! <<<");
    }

    Serial.printf("[SYNC] Remaining pending records: %d\n\n", countPendingRecords());
}

bool uploadBatchToBackend(String records[], int count) {
    if (count == 0) return true;

    HTTPClient http;
    http.setTimeout(SERVER_TIMEOUT_MS * 2);

    String url = "http://" + String(SERVER_HOST) + ":" + String(SERVER_PORT) + API_BATCH;
    if (!http.begin(url)) {
        return false;
    }

    http.addHeader("Content-Type", "application/json");

    // Construct Batch JSON Array
    String jsonPayload = "{\"device_id\":\"" + deviceId + "\",\"count\":" + String(count) + ",\"readings\":[";
    for (int i = 0; i < count; i++) {
        jsonPayload += records[i];
        if (i < count - 1) jsonPayload += ",";
    }
    jsonPayload += "]}";

    Serial.printf("[SYNC] Uploading batch of %d records to backend...\n", count);
    int httpCode = http.POST(jsonPayload);
    String response = http.getString();
    http.end();

    if (httpCode == 200 || httpCode == 201) {
        Serial.printf("[SYNC] Batch upload SUCCESS (HTTP %d): %s\n", httpCode, response.c_str());
        return true;
    } else {
        Serial.printf("[SYNC] Batch upload FAILED (HTTP %d)\n", httpCode);
        return false;
    }
}
