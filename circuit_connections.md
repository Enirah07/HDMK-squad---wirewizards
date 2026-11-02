# Circuit Connections and Hardware Interfacing Guide
## Reliable IoT Data Transmission During Network Failure

---

## 1. Hardware Bill of Materials (BOM)

| Component | Quantity | Specification / Description | Purpose |
|---|:---:|---|---|
| **ESP32 DevKit V1** | 1 | 30-Pin ESP-WROOM-32 Microcontroller | Core processing, Wi-Fi, LittleFS flash storage |
| **DHT22 (AM2302)** | 1 | Digital Temperature & Humidity Sensor (3-pin or 4-pin) | Ambient temperature and humidity sensing |
| **Active 5V / 3.3V Buzzer** | 1 | Continuous tone active piezoelectric sounder | Offline-only acoustic emergency alert |
| **NPN Transistor** | 1 | 2N2222, BC547, or S8050 | Buzzer current amplifier / driver |
| **Resistor 1 kΩ** | 1 | 1/4W Through-hole resistor | Transistor base current limiter |
| **Resistor 10 kΩ** | 1 | 1/4W (Required only for bare 4-pin DHT22) | Data bus pull-up to 3.3V |
| **Flyback Diode** | 1 | 1N4148 or 1N4007 (Recommended) | Snub inductive kickback from electromagnetic buzzer |
| **Solderless Breadboard** | 1 | Standard 400 or 830 tie-point | Component prototyping |
| **Jumper Wires** | 10–12 | Male-to-Male and Male-to-Female | Circuit interconnects |
| **Micro-USB Cable** | 1 | High-quality data & power cable | Power supply (5V) & serial programming |

---

## 2. ESP32 DevKit V1 Pin Diagram & GPIO Allocation

```
                   ESP32 DevKit V1 (30-Pin)
                         +----------+
                   EN ---|          |--- GPIO 23
         (VP) GPIO 36 ---|          |--- GPIO 22
         (VN) GPIO 39 ---|          |--- GPIO 1  (TX0)
              GPIO 34 ---|          |--- GPIO 3  (RX0)
              GPIO 35 ---|          |--- GPIO 21
              GPIO 32 ---|          |--- GPIO 19
              GPIO 33 ---|          |--- GPIO 18  --> [BUZZER DRIVER]
              GPIO 25 ---|          |--- GPIO 5
              GPIO 26 ---|          |--- GPIO 17
              GPIO 27 ---|          |--- GPIO 16
              GPIO 14 ---|          |--- GPIO 4   <-- [DHT22 DATA]
              GPIO 12 ---|          |--- GPIO 0
              GPIO 13 ---|          |--- GPIO 2   --> [STATUS LED]
                  GND ---|          |--- GPIO 15
                  VIN ---|          |--- 3V3      --> [3.3V POWER RAIL]
                         +----------+
```

### GPIO Assignment Matrix

| Function | ESP32 Pin | I/O Type | Signal Logic | Safe Selection Rationale |
|---|:---:|:---:|:---:|---|
| **DHT22 Data Bus** | **GPIO 4** | Bidirectional | 3.3V Logic | Standard GPIO; no strapping conflict; safe for high-frequency pulse timing. |
| **Buzzer Trigger** | **GPIO 18** | Output | HIGH = Alarm, LOW = Quiet | General purpose output; no boot-state pull interference; supports PWM or direct logic. |
| **Sensor Power** | **3V3** | Power Out | +3.3V DC | Regulated 3.3V rail from on-board AMS1117 LDO. Prevents logic-level mismatch. |
| **Common Ground** | **GND** | Ground | 0V Reference | Common reference for ESP32, DHT22, transistor emitter, and buzzer. |
| **Buzzer High-V (Optional)** | **VIN (or 3V3)** | Power Out | +5V DC (from USB) | Supplies higher acoustic decibels for 5V buzzers via transistor collector. |

---

## 3. DHT22 Sensor Wiring: Bare Sensor vs Breakout Module

### Critical Difference:
- **Bare DHT22 / AM2302 (4 Pins):**
  - Pin 1: VCC (+3.3V)
  - Pin 2: DATA (Requires an **external 10 kΩ pull-up resistor** connected between Pin 1 and Pin 2)
  - Pin 3: NC (Not Connected)
  - Pin 4: GND (0V)
- **DHT22 Breakout Module (3 Pins on a small PCB):**
  - Includes a surface-mount 10 kΩ pull-up resistor already soldered on the PCB!
  - Pin '+': Connect to ESP32 **3V3**
  - Pin '-': Connect to ESP32 **GND**
  - Pin 'OUT' / 'DAT': Connect to ESP32 **GPIO 4** directly.

> [!IMPORTANT]
> Always power DHT22 from the ESP32's **3.3V (3V3)** pin rather than 5V/VIN. This guarantees that DHT22's data output high logic level never exceeds 3.3V, completely protecting the ESP32 GPIO from over-voltage damage.

---

## 4. Buzzer Interfacing: Transistor Driver Circuit

Active buzzers typically draw 25 mA to 45 mA during sounding. While the ESP32 GPIO pin can safely supply a maximum of 12 mA to 20 mA, driving a buzzer directly from a GPIO pin can cause voltage sags, brownout resets, or long-term damage to the internal output stage.

### Recommended Transistor Driver Circuit:
```
         +3.3V or VIN (+5V)
               |
             +---+
             | + | Active Buzzer
             | - |
             +---+
               |
               +-------+ (Collector)
               |       |
             [ |<--|   | Diode 1N4148 (Cathode to +V, Anode to Collector)
               |   NPN (2N2222 / BC547)
               |
  GPIO 18 ----[ 1 kΩ ]---- Base
                           |
                        (Emitter)
                           |
                          GND
```

### Wiring Steps:
1. Connect ESP32 **GPIO 18** to one lead of the **1 kΩ resistor**.
2. Connect the other lead of the 1 kΩ resistor to the **Base (Pin B)** of the NPN transistor (Middle pin on 2N2222 / BC547).
3. Connect the **Emitter (Pin E)** of the transistor to ESP32 **GND**.
4. Connect the **Collector (Pin C)** of the transistor to the **Negative (-) pin** of the Active Buzzer.
5. Connect the **Positive (+) pin** of the Active Buzzer to ESP32 **VIN (5V)** or **3V3**.
6. (Recommended) Place a 1N4148 or 1N4007 diode in parallel with the buzzer pins (Cathode/band to (+), Anode to (-)) to snub flyback spikes.

---

## 5. Complete Breadboard Wiring Guide

```
+----------------------------------------------------------------------------------+
| COMPONENT         PIN / TERMINAL           ESP32 DEVKIT V1 CONNECTION            |
+----------------------------------------------------------------------------------+
| DHT22 Sensor      VCC (Pin 1 or '+')  -->  ESP32 3V3 (3.3V Rail)                 |
|                   DATA (Pin 2 / 'OUT') -->  ESP32 GPIO 4                         |
|                   GND (Pin 4 or '-')  -->  ESP32 GND                             |
|                   (10k Resistor)       -->  Between 3.3V and GPIO 4 (Bare DHT22) |
+----------------------------------------------------------------------------------+
| Buzzer Subsystem  Resistor 1 kΩ (In)   -->  ESP32 GPIO 18                         |
|                   Resistor 1 kΩ (Out)  -->  Transistor Base (B)                  |
|                   Transistor Emitter   -->  ESP32 GND                            |
|                   Transistor Collector -->  Buzzer Negative (-) Terminal         |
|                   Buzzer Positive (+)  -->  ESP32 VIN (+5V) or 3V3               |
+----------------------------------------------------------------------------------+
| Micro-USB Cable   USB-A Plug           -->  Laptop / USB 5V 1A Power Adapter     |
|                   Micro-B Plug         -->  ESP32 DevKit V1 USB Receptacle       |
+----------------------------------------------------------------------------------+
```

---

## 6. Hardware Safety Rules & GPIOs to Avoid

> [!CAUTION]
> Avoid using the following pins for sensors or actuators:
> - **GPIO 6 to 11**: Internally hardwired to ESP32 integrated SPI Flash memory. Connecting external hardware here will instantly crash the ESP32!
> - **GPIO 0, 2, 12, 15**: Bootstrapping pins. Pulling these pins HIGH or LOW externally during power-up will prevent firmware booting or force download mode.
> - **GPIO 34, 35, 36, 39**: Input-only pins without internal pull-ups or output drivers. Cannot drive buzzers or single-wire bidirectional DHT sensors.
