# DataBuddy Turbine Bench Wiring

## Hardware list (with part names / numbers)

- Teensy 3.6 (DataBuddy logger/UI MCU)
- Arduino Nano ESP32 with headers (ABX00083 / DigiKey 1050-ABX00083-ND) (sensor MCU)
- Adafruit ADS1115 breakout, I2C address `0x48` (DigiKey 1528-1085-ND)
- 2x Grundfos RPS 0-16 bar pressure transducer (voltage output)
- Allegro A1220 hall-effect latch (flow pulse pickup) + 10kΩ pull-up resistor
- PC817X2NSZ9F optocoupler (DigiKey PC817X2NSZ9F-ND) + series resistor on LED side
- 4x 1N5819G Schottky diodes (DigiKey 1N5819GOS-ND) as bridge rectifier
- Smoothing capacitor across rectified DC bus (electrolytic, value per bench setup)
- Resistor divider from DC bus to ADS1115 A2 (for optional voltage logging)
- DL24 electronic load (150W DC USB electronic load/tester) on DC bus
- USB-C pigtail power input + USB-A breakout (Teensy plugs into USB-A)
- Shared 5V/GND wiring harness between Teensy/Nano/sensors
- Nano status display (I2C OLED in current SensorMCU sketch, addr `0x3C`)
- 1x momentary push button on Nano ESP32 D4 (page toggle), no second rotary encoder needed

## System wiring overview (ASCII)

```text
Portable charger / phone / USB source
        │
        ▼
   USB-C pigtail (5V, GND)
        │
        ▼
    USB-A breakout  ───────────────► Teensy 3.6 (USB power + USB data if used)
        │
        ├── 5V rail ───────────────► Nano ESP32 5V/VBUS
        ├── 5V rail ───────────────► Grundfos RPS #1 Brown (+5V)
        ├── 5V rail ───────────────► Grundfos RPS #2 Brown (+5V)
        └── GND rail ──────────────► All grounds common (Teensy, Nano, ADS1115, sensors)

Nano ESP32 (sensor MCU):
  SDA/SCL ───────────────► ADS1115 SDA/SCL (I2C, addr 0x48)
  SDA/SCL ───────────────► Nano status display SDA/SCL (I2C, addr 0x3C)
  D2 (interrupt) ◄─────── A1220 OUT (with 10k pull-up to VCC)
  D3 (interrupt) ◄─────── PC817 transistor output (pull-up to MCU logic rail)
  D4 ◄─────────────────── momentary button to GND (INPUT_PULLUP, press = LOW)
  TX (Serial1) ──────────► Teensy Serial1 RX
  GND ───────────────────► Teensy GND (shared reference)

ADS1115 analog inputs:
  A0 ◄─────────────────── Grundfos P1 White (signal)
  A1 ◄─────────────────── Grundfos P2 White (signal)
  A2 ◄─────────────────── DC bus divider midpoint (optional generator DC sense)

Generator branch:
  Turbine AC ─► PC817 LED + series resistor ─► (isolated pulse output to Nano D3)
  Turbine AC ─► 1N5819 x4 Schottky bridge ─► smoothing capacitor ─► DC bus ─► DL24 load
                                                 │
                                                 └── divider ─► ADS1115 A2 (optional logging)
```

## Sensor pinouts / details

### Grundfos RPS pressure transducer (voltage output)
- Pin 4 (Brown): +5V
- Pin 3 (Green): GND
- Pin 2 (White): Analog signal (to ADS1115 A0/A1)
- Pin 1 (Yellow): Temperature output (unused in this firmware)

### Allegro A1220 flow pickup
- VCC
- GND
- OUT → Nano ESP32 D2 interrupt input
- Add 10kΩ pull-up from OUT to VCC

### PC817 optocoupler frequency pickup
- Generator AC (through appropriate series resistor/current limit) drives PC817 LED side
- PC817 transistor side provides isolated pulse output to Nano ESP32 D3
- Use pull-up on transistor output to logic rail

### 1N5819 Schottky bridge
- 4 diodes arranged as full bridge from turbine AC to DC bus
- Smoothing capacitor across DC+ and DC-
- DL24 connects across this DC bus

## UART link (sensor MCU to logger MCU)

- Nano ESP32 UART TX (`Serial1 TX`) → Teensy 3.6 `Serial1 RX`
- Shared GND required between Nano and Teensy
- CSV stream format:
  `P1_voltage,P2_voltage,flow_hz,gen_freq_hz,dc_voltage`

## Nano local display + button behavior

- Nano display is used as a local readback/system-check display (so no second rotary encoder is required).
- Momentary side button on Nano D4 toggles pages:
  - System check
  - Delta P
  - Flow rate
  - Frequency
  - Voltage
  - Resistance (informational note: resistance is set on DL24, not measured by Nano)

## Power notes

- Bench electronics in this setup run from 5V USB power (USB-C pigtail to USB-A breakout).
- Grundfos sensors in this setup are voltage-output sensors, so 5V supply is appropriate.
- If changing to 4-20mA sensors later, loop-power requirements are different and may require higher excitation voltage.
