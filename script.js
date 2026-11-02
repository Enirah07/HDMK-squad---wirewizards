/**
 * Reliable IoT Data Transmission System - Dashboard Controller
 * Handles real-time polling, Chart.js telemetry visualization,
 * offline recovery synchronization audit, and buzzer status tracking.
 */

// Auto-detect API base: allows dashboard to work whether opened via http://localhost:5000,
// Live Server (port 5500), or directly as a file://
const API_BASE = (window.location.protocol === 'file:' || (window.location.port !== '5000' && window.location.port !== '')) 
    ? 'http://localhost:5000' 
    : '';

// Configuration
const CONFIG = {
    POLL_INTERVAL_MS: 2500,
    ALERT_THRESHOLD_TEMP: 35.0,
    API: {
        LATEST: API_BASE + '/api/readings/latest',
        HISTORY: API_BASE + '/api/readings/history',
        RECOVERED: API_BASE + '/api/readings/recovered',
        STATUS: API_BASE + '/api/device/status',
        RESET: API_BASE + '/api/admin/reset'
    }
};

// Application State
let tempChartInstance = null;
let humChartInstance = null;
let pollTimer = null;
let cachedRecoveredRecords = [];

// DOM Element References
const elements = {
    statusDot: document.getElementById('statusDot'),
    statusText: document.getElementById('statusText'),
    clockDisplay: document.getElementById('clockDisplay'),
    valTemperature: document.getElementById('valTemperature'),
    valHumidity: document.getElementById('valHumidity'),
    valRecoveredCount: document.getElementById('valRecoveredCount'),
    valTotalRecords: document.getElementById('valTotalRecords'),
    tempBadge: document.getElementById('tempBadge'),
    tempMinMax: document.getElementById('tempMinMax'),
    humMinMax: document.getElementById('humMinMax'),
    cardTemp: document.getElementById('cardTemp'),
    cardBuzzer: document.getElementById('cardBuzzer'),
    buzzerStatePill: document.getElementById('buzzerStatePill'),
    buzzerStatusTitle: document.getElementById('buzzerStatusTitle'),
    stepOnline: document.getElementById('stepOnline'),
    stepOffline: document.getElementById('stepOffline'),
    stepSync: document.getElementById('stepSync'),
    recoveredTableBody: document.getElementById('recoveredTableBody'),
    tableRecordCount: document.getElementById('tableRecordCount'),
    eventFeed: document.getElementById('eventFeed'),
    btnRefresh: document.getElementById('btnRefresh'),
    btnResetDb: document.getElementById('btnResetDb'),
    btnExportCsv: document.getElementById('btnExportCsv')
};

// ============================================================================
// INITIALIZATION
// ============================================================================

document.addEventListener('DOMContentLoaded', () => {
    initCharts();
    initClock();
    attachEventListeners();
    fetchTelemetry();
    startPolling();
});

function initClock() {
    function updateClock() {
        const now = new Date();
        elements.clockDisplay.textContent = now.toTimeString().split(' ')[0] + ' ' + (now.toLocaleDateString());
    }
    updateClock();
    setInterval(updateClock, 1000);
}

function attachEventListeners() {
    elements.btnRefresh.addEventListener('click', () => {
        elements.btnRefresh.textContent = 'Refreshing...';
        fetchTelemetry().finally(() => {
            elements.btnRefresh.textContent = '↻ Refresh';
        });
    });

    elements.btnResetDb.addEventListener('click', async () => {
        if (!confirm('Are you sure you want to reset all readings and start a fresh demonstration?')) {
            return;
        }
        try {
            const res = await fetch(CONFIG.API.RESET, { method: 'POST' });
            if (res.ok) {
                fetchTelemetry();
            }
        } catch (e) {
            console.error('Reset error:', e);
        }
    });

    elements.btnExportCsv.addEventListener('click', exportRecoveredCsv);

    // Simulator Toolbar Listeners
    const btnSimStart = document.getElementById('btnSimStart');
    const btnSimDrop = document.getElementById('btnSimDropNetwork');
    const btnSimSpike = document.getElementById('btnSimSpikeTemp');
    const btnSimRecover = document.getElementById('btnSimRecover');
    const btnSimStop = document.getElementById('btnSimStop');

    if (btnSimStart) btnSimStart.addEventListener('click', startSimulator);
    if (btnSimDrop) btnSimDrop.addEventListener('click', simulateNetworkDrop);
    if (btnSimSpike) btnSimSpike.addEventListener('click', simulateSpikeTemp);
    if (btnSimRecover) btnSimRecover.addEventListener('click', simulateReconnectAndSync);
    if (btnSimStop) btnSimStop.addEventListener('click', stopSimulator);
}

// ============================================================================
// CHART.JS INITIALIZATION
// ============================================================================

function initCharts() {
    // Shared chart options
    const commonOptions = {
        responsive: true,
        maintainAspectRatio: false,
        animation: { duration: 600 },
        interaction: { mode: 'index', intersect: false },
        scales: {
            x: {
                grid: { color: 'rgba(255, 255, 255, 0.05)' },
                ticks: { color: '#94a3b8', font: { family: 'Roboto Mono', size: 10 } }
            },
            y: {
                grid: { color: 'rgba(255, 255, 255, 0.05)' },
                ticks: { color: '#94a3b8', font: { family: 'Roboto Mono', size: 11 } }
            }
        },
        plugins: {
            legend: { display: false },
            tooltip: {
                backgroundColor: 'rgba(12, 20, 36, 0.95)',
                titleFont: { family: 'Orbitron', size: 12 },
                bodyFont: { family: 'Roboto Mono', size: 12 },
                borderColor: '#22d3ee',
                borderWidth: 1,
                padding: 10,
                callbacks: {
                    afterLabel: function(context) {
                        const raw = context.raw;
                        if (raw && raw.is_recovered) {
                            return '⚡ LittleFS Flash Recovered';
                        }
                        return '● Live Streamed';
                    }
                }
            }
        }
    };

    // Temperature Chart with 35°C Threshold Line
    const ctxTemp = document.getElementById('tempChart').getContext('2d');
    tempChartInstance = new Chart(ctxTemp, {
        type: 'line',
        data: {
            labels: [],
            datasets: [
                {
                    label: 'Temperature (°C)',
                    data: [],
                    borderColor: '#06b6d4',
                    backgroundColor: 'rgba(6, 182, 212, 0.1)',
                    borderWidth: 2,
                    fill: true,
                    tension: 0.3,
                    pointBackgroundColor: [],
                    pointBorderColor: [],
                    pointRadius: [],
                    pointStyle: []
                },
                {
                    label: 'Alert Threshold (35°C)',
                    data: [],
                    borderColor: '#ef4444',
                    borderWidth: 1.5,
                    borderDash: [6, 4],
                    pointRadius: 0,
                    fill: false
                }
            ]
        },
        options: {
            ...commonOptions,
            scales: {
                ...commonOptions.scales,
                y: {
                    ...commonOptions.scales.y,
                    suggestedMin: 20,
                    suggestedMax: 45
                }
            }
        }
    });

    // Humidity Chart
    const ctxHum = document.getElementById('humChart').getContext('2d');
    humChartInstance = new Chart(ctxHum, {
        type: 'line',
        data: {
            labels: [],
            datasets: [{
                label: 'Humidity (%)',
                data: [],
                borderColor: '#3b82f6',
                backgroundColor: 'rgba(59, 130, 246, 0.12)',
                borderWidth: 2,
                fill: true,
                tension: 0.3,
                pointBackgroundColor: '#3b82f6',
                pointRadius: 3
            }]
        },
        options: {
            ...commonOptions,
            scales: {
                ...commonOptions.scales,
                y: {
                    ...commonOptions.scales.y,
                    suggestedMin: 30,
                    suggestedMax: 90
                }
            }
        }
    });
}

// ============================================================================
// REAL-TIME POLLING ENGINE
// ============================================================================

function startPolling() {
    if (pollTimer) clearInterval(pollTimer);
    pollTimer = setInterval(fetchTelemetry, CONFIG.POLL_INTERVAL_MS);
}

async function fetchTelemetry() {
    try {
        const [latestRes, historyRes, recoveredRes, statusRes] = await Promise.all([
            fetch(CONFIG.API.LATEST),
            fetch(`${CONFIG.API.HISTORY}?limit=35`),
            fetch(`${CONFIG.API.RECOVERED}?limit=50`),
            fetch(CONFIG.API.STATUS)
        ]);

        if (latestRes.ok) {
            const latestData = await latestRes.json();
            updateLiveMetrics(latestData);
        }

        if (historyRes.ok) {
            const historyData = await historyRes.json();
            updateCharts(historyData.data || []);
        }

        if (recoveredRes.ok) {
            const recData = await recoveredRes.json();
            cachedRecoveredRecords = recData.data || [];
            updateRecoveredTable(cachedRecoveredRecords);
        }

        if (statusRes.ok) {
            const statusData = await statusRes.json();
            updateEventFeed(statusData.recent_events || []);
        }

    } catch (err) {
        console.warn('Backend connection polling issue:', err);
        setSystemOfflineVisuals();
    }
}

// ============================================================================
// METRIC & UI STATE UPDATES
// ============================================================================

function updateLiveMetrics(data) {
    const reading = data.reading;
    const devStatus = data.device_status || {};
    const isOnline = devStatus.status === 'ONLINE';

    // 1. Connection Status Badge
    if (isOnline) {
        elements.statusDot.className = 'status-dot dot-online';
        elements.statusText.textContent = 'ONLINE (ESP32 LINKED)';
    } else {
        elements.statusDot.className = 'status-dot dot-offline';
        const delayStr = devStatus.seconds_since_last_seen ? ` (${devStatus.seconds_since_last_seen}s ago)` : '';
        elements.statusText.textContent = `OFFLINE${delayStr}`;
    }

    // 2. Total and Recovered Counts
    elements.valTotalRecords.textContent = devStatus.total_readings || 0;
    elements.valRecoveredCount.textContent = devStatus.total_recovered || 0;

    // 3. Sensor Values
    if (reading) {
        const temp = parseFloat(reading.temperature);
        const hum = parseFloat(reading.humidity);

        elements.valTemperature.textContent = temp.toFixed(1);
        elements.valHumidity.textContent = hum.toFixed(1);

        // Temperature Alert Logic & Buzzer State Presentation
        handleAlertAndBuzzerState(temp, isOnline, devStatus);
    } else {
        elements.valTemperature.textContent = '--.-';
        elements.valHumidity.textContent = '--.-';
    }

    // 4. Update Architecture Banner Flow
    updateFlowBanner(isOnline, devStatus);
}

function handleAlertAndBuzzerState(temp, isOnline, devStatus) {
    const isHighTemp = temp >= CONFIG.ALERT_THRESHOLD_TEMP;

    // RULE: Online -> Buzzer is ALWAYS OFF regardless of temperature
    // OFFLINE -> Buzzer is ON only if temp >= 35.0°C
    if (isOnline) {
        // ONLINE MODE
        elements.cardTemp.classList.remove('temp-alert-active');
        elements.tempBadge.className = 'metric-badge';
        elements.tempBadge.textContent = 'ONLINE (SAFE)';
        
        // Buzzer state
        elements.cardBuzzer.classList.remove('buzzer-active');
        elements.buzzerStatePill.className = 'metric-badge badge-dim';
        elements.buzzerStatePill.textContent = 'OFF';
        elements.buzzerStatusTitle.textContent = 'QUIET (ONLINE MODE)';
        elements.buzzerStatusTitle.style.color = '#94a3b8';
    } else {
        // OFFLINE MODE
        if (isHighTemp) {
            // CRITICAL OFFLINE THRESHOLD EXCEEDED
            elements.cardTemp.classList.add('temp-alert-active');
            elements.tempBadge.className = 'metric-badge badge-alert';
            elements.tempBadge.textContent = 'OFFLINE ALERT (>35°C)';

            elements.cardBuzzer.classList.add('buzzer-active');
            elements.buzzerStatePill.className = 'metric-badge badge-alert';
            elements.buzzerStatePill.textContent = 'ACTIVE';
            elements.buzzerStatusTitle.textContent = 'ALARM ON (>35°C OFFLINE)';
        } else {
            // OFFLINE NORMAL TEMPERATURE
            elements.cardTemp.classList.remove('temp-alert-active');
            elements.tempBadge.className = 'metric-badge';
            elements.tempBadge.textContent = 'OFFLINE (NORMAL)';

            elements.cardBuzzer.classList.remove('buzzer-active');
            elements.buzzerStatePill.className = 'metric-badge badge-dim';
            elements.buzzerStatePill.textContent = 'OFF';
            elements.buzzerStatusTitle.textContent = 'STANDBY (TEMP SAFE)';
            elements.buzzerStatusTitle.style.color = '#94a3b8';
        }
    }
}

function updateFlowBanner(isOnline, devStatus) {
    elements.stepOnline.classList.remove('active');
    elements.stepOffline.classList.remove('active', 'active-alert');
    elements.stepSync.classList.remove('active');

    if (isOnline) {
        if (devStatus.total_recovered > 0 && devStatus.seconds_since_last_seen < 15) {
            elements.stepSync.classList.add('active');
        } else {
            elements.stepOnline.classList.add('active');
        }
    } else {
        elements.stepOffline.classList.add('active');
    }
}

function setSystemOfflineVisuals() {
    elements.statusDot.className = 'status-dot dot-offline';
    elements.statusText.textContent = 'BACKEND OFFLINE';
}

// ============================================================================
// CHART UPDATES
// ============================================================================

function updateCharts(history) {
    if (!tempChartInstance || !humChartInstance || history.length === 0) return;

    const labels = [];
    const tempData = [];
    const humData = [];
    const pointColors = [];
    const pointRadii = [];
    const pointStyles = [];
    const thresholdData = [];

    let minT = 999, maxT = -999;
    let minH = 999, maxH = -999;

    history.forEach(item => {
        // Format time label (HH:MM:SS)
        const tStr = item.iso_timestamp ? item.iso_timestamp.substring(11, 19) : '--:--:--';
        labels.push(tStr);

        const tVal = parseFloat(item.temperature);
        const hVal = parseFloat(item.humidity);

        tempData.push({ x: tStr, y: tVal, is_recovered: item.is_offline_recovered === 1 });
        humData.push(hVal);
        thresholdData.push(CONFIG.ALERT_THRESHOLD_TEMP);

        if (tVal < minT) minT = tVal;
        if (tVal > maxT) maxT = tVal;
        if (hVal < minH) minH = hVal;
        if (hVal > maxH) maxH = hVal;

        // Custom style for recovered LittleFS data points!
        if (item.is_offline_recovered === 1) {
            pointColors.push('#c084fc');
            pointRadii.push(6);
            pointStyles.push('rectRot'); // Diamond
        } else {
            pointColors.push('#06b6d4');
            pointRadii.push(3);
            pointStyles.push('circle');
        }
    });

    // Update Min/Max labels
    if (minT !== 999) {
        elements.tempMinMax.textContent = `Min: ${minT.toFixed(1)}°C / Max: ${maxT.toFixed(1)}°C`;
    }
    if (minH !== 999) {
        elements.humMinMax.textContent = `Min: ${minH.toFixed(1)}% / Max: ${maxH.toFixed(1)}%`;
    }

    // Temperature Chart Update
    tempChartInstance.data.labels = labels;
    tempChartInstance.data.datasets[0].data = tempData;
    tempChartInstance.data.datasets[0].pointBackgroundColor = pointColors;
    tempChartInstance.data.datasets[0].pointBorderColor = pointColors;
    tempChartInstance.data.datasets[0].pointRadius = pointRadii;
    tempChartInstance.data.datasets[0].pointStyle = pointStyles;
    tempChartInstance.data.datasets[1].data = thresholdData;
    tempChartInstance.update('none');

    // Humidity Chart Update
    humChartInstance.data.labels = labels;
    humChartInstance.data.datasets[0].data = humData;
    humChartInstance.update('none');
}

// ============================================================================
// RECOVERED TABLE & EVENT FEED
// ============================================================================

function updateRecoveredTable(records) {
    elements.tableRecordCount.textContent = `${records.length} Records`;

    if (!records || records.length === 0) {
        elements.recoveredTableBody.innerHTML = `
            <tr>
                <td colspan="6" class="empty-state">
                    No offline recovered records yet. Disconnect Wi-Fi to test LittleFS buffer!
                </td>
            </tr>
        `;
        return;
    }

    const rowsHtml = records.slice(0, 50).map(r => {
        const origTime = r.iso_timestamp ? r.iso_timestamp.replace('T', ' ').replace('Z', '') : '--';
        const srvTime = r.server_received_at ? r.server_received_at : '--';
        return `
            <tr>
                <td><strong>${escapeHtml(r.record_id)}</strong></td>
                <td>${escapeHtml(origTime)}</td>
                <td style="color: ${r.temperature >= 35 ? '#f87171' : '#22d3ee'}; font-weight: 600;">
                    ${parseFloat(r.temperature).toFixed(1)}°C
                </td>
                <td>${parseFloat(r.humidity).toFixed(1)}%</td>
                <td style="color: #94a3b8;">${escapeHtml(srvTime)}</td>
                <td><span class="badge-tag-flash">⚡ LittleFS Synced</span></td>
            </tr>
        `;
    }).join('');

    elements.recoveredTableBody.innerHTML = rowsHtml;
}

function updateEventFeed(events) {
    if (!events || events.length === 0) return;

    const html = events.map(e => {
        let typeClass = 'event-info';
        if (e.event_type === 'SYNC_RECOVERY') typeClass = 'event-sync';
        if (e.event_type === 'DISCONNECTED') typeClass = 'event-disconnect';
        if (e.event_type === 'ALERT') typeClass = 'event-alert';

        const timeStr = e.timestamp ? e.timestamp.substring(11, 19) : '--:--:--';
        return `
            <div class="event-item ${typeClass}">
                <span class="event-time">${escapeHtml(timeStr)}</span>
                <span class="event-desc">${escapeHtml(e.details || e.event_type)}</span>
            </div>
        `;
    }).join('');

    elements.eventFeed.innerHTML = html;
}

// ============================================================================
// CSV EXPORT FOR JUDGES DEMO
// ============================================================================

function exportRecoveredCsv() {
    if (!cachedRecoveredRecords || cachedRecoveredRecords.length === 0) {
        alert('No recovered LittleFS records to export yet.');
        return;
    }

    const headers = ['Record ID', 'Device ID', 'Temperature (C)', 'Humidity (%)', 'Sensor Timestamp', 'Sensor ISO Time', 'Server Ingested At', 'LittleFS Recovered Flag'];
    const rows = cachedRecoveredRecords.map(r => [
        `"${r.record_id}"`,
        `"${r.device_id}"`,
        r.temperature,
        r.humidity,
        r.timestamp,
        `"${r.iso_timestamp}"`,
        `"${r.server_received_at}"`,
        r.is_offline_recovered
    ]);

    const csvContent = 'data:text/csv;charset=utf-8,' + [headers.join(','), ...rows.map(e => e.join(','))].join('\n');
    const encodedUri = encodeURI(csvContent);
    const link = document.createElement('a');
    link.setAttribute('href', encodedUri);
    link.setAttribute('download', `LittleFS_Recovered_Readings_${Date.now()}.csv`);
    document.body.appendChild(link);
    link.click();
    document.body.removeChild(link);
}

function escapeHtml(str) {
    if (!str) return '';
    return String(str)
        .replace(/&/g, '&amp;')
        .replace(/</g, '&lt;')
        .replace(/>/g, '&gt;')
        .replace(/"/g, '&quot;');
}

// ============================================================================
// HACKATHON LIVE DEMO SIMULATOR ENGINE
// Generates realistic varying telemetry & simulates network outages and recovery
// ============================================================================
let simState = 'IDLE'; // 'IDLE', 'ONLINE', 'OFFLINE', 'OFFLINE_ALERT'
let simInterval = null;
let simStep = 0;
let simSeq = 100;
let simOfflineQueue = [];
let simBaseTemp = 29.5;
let simBaseHum = 64.0;

function startSimulator() {
    if (simInterval) clearInterval(simInterval);
    simState = 'ONLINE';
    updateSimulatorButtons();
    
    // Send immediate first reading, then every 2500ms
    generateAndTransmitSimPoint();
    simInterval = setInterval(generateAndTransmitSimPoint, 2500);
}

function stopSimulator() {
    if (simInterval) {
        clearInterval(simInterval);
        simInterval = null;
    }
    simState = 'IDLE';
    updateSimulatorButtons();
}

function simulateNetworkDrop() {
    simState = 'OFFLINE';
    updateSimulatorButtons();
    // Mark device offline immediately on UI
    elements.statusDot.className = 'status-dot dot-offline';
    elements.statusText.textContent = 'OFFLINE (NETWORK DROPPED)';
    updateFlowBanner(false, { total_recovered: simOfflineQueue.length });
}

function simulateSpikeTemp() {
    simState = 'OFFLINE_ALERT';
    updateSimulatorButtons();
    // Force immediate generation of high temp reading (>35°C)
    generateAndTransmitSimPoint(true);
}

async function simulateReconnectAndSync() {
    simState = 'ONLINE';
    updateSimulatorButtons();

    // Silence buzzer immediately
    elements.cardBuzzer.classList.remove('buzzer-active');
    elements.buzzerStatePill.className = 'metric-badge badge-dim';
    elements.buzzerStatePill.textContent = 'OFF';
    elements.buzzerStatusTitle.textContent = 'QUIET (ONLINE MODE)';
    elements.buzzerStatusTitle.style.color = '#94a3b8';

    // Highlight sync step
    elements.stepSync.classList.add('active');
    elements.statusDot.className = 'status-dot dot-sync';
    elements.statusText.textContent = `SYNCING ${simOfflineQueue.length} FLASH RECORDS...`;

    // Upload queued records in batch to backend
    if (simOfflineQueue.length > 0) {
        try {
            const batchUrl = CONFIG.API.RECOVERED ? CONFIG.API.RECOVERED.replace('/recovered', '/batch') : '/api/readings/batch';
            await fetch(batchUrl, {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify({
                    device_id: 'ESP32_F924F0',
                    count: simOfflineQueue.length,
                    readings: simOfflineQueue
                })
            });
            simOfflineQueue = [];
        } catch (e) {
            console.error('Batch sync error:', e);
        }
    }

    // Refresh telemetry from server
    await fetchTelemetry();
}

async function generateAndTransmitSimPoint(forceSpike = false) {
    simStep += 0.35;
    simSeq++;

    // Realistic sinusoidal walk + small random noise
    let temp = simBaseTemp + Math.sin(simStep) * 2.5 + (Math.random() * 0.8 - 0.4);
    let hum = simBaseHum + Math.cos(simStep) * 4.5 + (Math.random() * 1.4 - 0.7);

    // If in alert state or spike forced, push temperature above 35°C
    if (simState === 'OFFLINE_ALERT' || forceSpike) {
        temp = 36.5 + (Math.random() * 2.5); // 36.5°C to 39.0°C
    }

    temp = Math.round(temp * 10) / 10;
    hum = Math.round(hum * 10) / 10;

    const now = new Date();
    const recordId = `ESP32_F924F0_S${Math.floor(simSeq / 100)}_${String(simSeq).padStart(5, '0')}`;
    const isoTimestamp = now.toISOString();
    const timestamp = Math.floor(now.getTime() / 1000);

    const record = {
        record_id: recordId,
        device_id: 'ESP32_F924F0',
        temperature: temp,
        humidity: hum,
        timestamp: timestamp,
        iso_timestamp: isoTimestamp,
        is_offline_recovered: 0
    };

    if (simState === 'ONLINE') {
        // Transmit live to Flask backend
        try {
            const postUrl = CONFIG.API.LATEST ? CONFIG.API.LATEST.replace('/latest', '') : '/api/readings';
            await fetch(postUrl, {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify(record)
            });
        } catch (err) {
            console.warn('Live transmission simulation error:', err);
        }
        // Poll and update charts
        fetchTelemetry();
    } else {
        // OFFLINE OR OFFLINE_ALERT -> Buffer locally in memory (simulating LittleFS)
        simOfflineQueue.push({ ...record, is_offline_recovered: 1 });

        // Update live metric displays locally
        elements.valTemperature.textContent = temp.toFixed(1);
        elements.valHumidity.textContent = hum.toFixed(1);
        elements.valRecoveredCount.textContent = simOfflineQueue.length;

        // Evaluate buzzer alert (OFFLINE ONLY)
        handleAlertAndBuzzerState(temp, false, { status: 'OFFLINE' });
    }
}

function updateSimulatorButtons() {
    const btnStart = document.getElementById('btnSimStart');
    const btnDrop = document.getElementById('btnSimDropNetwork');
    const btnSpike = document.getElementById('btnSimSpikeTemp');

    if (!btnStart) return;

    if (simState === 'ONLINE') {
        btnStart.textContent = '● Streaming Live...';
        btnStart.style.opacity = '0.7';
    } else {
        btnStart.textContent = '▶ Start Live Stream';
        btnStart.style.opacity = '1';
    }

    if (simState === 'OFFLINE' || simState === 'OFFLINE_ALERT') {
        btnDrop.textContent = `⚠️ Network Outage (${simOfflineQueue.length} in Flash)`;
    } else {
        btnDrop.textContent = '⚠️ Simulate Network Drop';
    }
}
