# DataBuddy

**DataBuddy** is a versatile Teensy-based data logging tool designed for sensor, manual entry, and valve test logging. It features an OLED display, rotary encoder for navigation and value entry, SD card data storage, and an optional battery monitoring system. DataBuddy makes any repeated data logging activity more efficient and is modular for different testing protocols.

---

## Features

- **OLED Display & Encoder Navigation:** Intuitive user interface with a 128x64 OLED screen and rotary encoder (with push-button).
- **Menu Modes:**
  - **Check Test Mode:** Guided logging for multiple valves and tests, loaded from SD card configuration.
  - **Basic Mode:** Manual entry of values in a table format.
  - **Sensor Mode:** Automated sensor value logging (customizable).
- **SD Card Logging:** Data saved in CSV format, readable by spreadsheet software.
- **Battery Percentage Reader (UI):** Always visible in the corner of the screen. Battery voltage sensing can be enabled or disabled in hardware/software.
- **Undo Last Log:** Double-click encoder to undo the last entry.


---

## Hardware Setup

### Teensy & Display

- **Microcontroller:** Teensy 3.6 (recommended)
- **OLED Display:** SSD1306, 128x64 via I2C (`Wire` library)
- **Rotary Encoder:** DT/CLK pins for increment, SW for push-button

### SD Card

- Uses the onboard SD slot of Teensy 3.6 (or compatible external SD adapter).

---

### Battery Monitoring (Optional)

- **Voltage Divider:** Connect battery + to analog pin via a resistor divider.
  - **Resistor Values:** Code is set for **10kΩ (R2)** and **2.7kΩ (R1)**.
  - **Wiring:** 
    ```
    Battery+ ----[2.7kΩ]----+----[10kΩ]---- GND
                           |
                        Analog Pin (e.g., A23)
    ```

- **Battery Status:** Percentage is calculated and displayed in the top-right corner. If battery sense hardware is removed, the UI still shows the percentage for future use, but values will not be accurate until hardware is restored.

---

## Check Test Mode

- **Config File:** `check_test_mode.txt` on SD card
  - Format: Each line = Valve name, Test type, Columns, Rows, Interval (minutes), [Optional label1], [Optional label2]
  - Example: `ValveA,Leak,2,10,1.5,Pressure,Flow`
- **Operation:** Select valve and test, follow prompts to enter values for each row at set intervals.

## Basic Mode

- **Manual Table Entry:** Set rows/columns, enter values using encoder, log to SD.

## Sensor Mode

- **Automated Logging:** Set duration, logs sensor data at fixed intervals (customize with your sensors).

---

## File Structure

- `datalog.csv`: All logged data (with headers and test details).
- `check_test_mode.txt`: Test configurations for valves.

---

## User Interface

- **Menu Navigation:** Rotate encoder to scroll, press to select.
- **Value Entry:** Rotate to change value, press to confirm column/log.
- **Battery %:** Displayed in the corner throughout all screens.
- **Undo:** Double-click encoder button to undo last log entry.

---

## Customization

- **Battery Divider:** Change resistor values in code and hardware to match your battery configuration.
- **Sensor Integration:** Add your sensor using the signal (yellow) and ground (blue). Have it post a comma seperated list and the DataBuddy will enter it in a CSV file.
- **Valve/Test Expansion:** Edit `check_test_mode.txt` for more valves/tests.

---

## Notes

- If SD card is missing, DataBuddy will show an error and return to menu.
- Battery percentage will read as "--%" if sensing hardware is removed.
- For full battery monitoring, ensure voltage divider is wired and `BATTERY_PIN` is set correctly in the code.

---

## Getting Started

1. Wire up the OLED, encoder, and optional battery divider.
2. Flash the Teensy with `DataBuddy_main.ino`.
3. Insert SD card, add `check_test_mode.txt` if using Check Test Mode.
4. Power on and follow the on-screen prompts!

---



---

**Questions or need customization?**  
Open an issue in [DataBuddy GitHub](https://github.com/alexcrespo98/DataBuddy) or contact your boy Alex.
