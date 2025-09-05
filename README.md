# DataBuddy

## To-Do / Roadmap

### Robustness & Reliability

- [ ] **Investigate and fix freezing issues**
  - Add debug printouts to localize freeze causes (SD card, serial, etc.).
  - Implement a watchdog timer to auto-recover from hangs.
  - Add timeouts/checks on SD and serial operations.
  - Test with multiple SD cards and ensure good power supply.

- [ ] **Graceful recovery after power loss**
  - Implement a checkpoint system: periodically save current state (mode, file, position) to SD or EEPROM.
  - On startup, detect if a checkpoint exists and offer to resume.
  - Ensure files are flushed/closed after each write to minimize data loss.

### Modes & Features

- [ ] **Current Modes**
  - Basic mode: manual data entry via rotary encoder.
  - Sensor mode: capture and log serial data from external TX pin.

- [ ] **Sensor Input Expansion**
  - Plan to support digital, analog, and serial data collection from a single pin (or selectable pin).
  - Consider using analogRead, digitalRead, and Serial in parallel with a mode selector.
  - If needed, add an additional pin to separate analog/digital from serial to avoid conflicts.
  - Standardize pin assignments and document them clearly.

- [ ] **Standardization and Ease-of-Use**
  - Develop clear menu/UI for selecting sensor type and pin.
  - Document pinout and usage for all supported modes.

### Stretch / Future Goals

- [ ] Support logging from multiple input types simultaneously.
- [ ] Expand file format support (CSV, JSON, etc.).
- [ ] Optional: Add BLE or WiFi for wireless syncing.
- [ ] Data visualization on OLED or via a Python script.

---

**Current Hardware & Operation**

DataBuddy is a Teensy 3.6-based hardware and software platform for flexible, interactive data logging. It supports two main operating modes: **Basic** (manual input) and **Sensor** (serial data stream capture). The device interacts with users via a rotary encoder and OLED display, and stores/transfers data using the Teensy’s onboard microSD card. DataBuddy is designed to interface with any microcontroller’s TX pin for serial data streaming, making it adaptable to a wide range of sensor or embedded applications.

---

### Major Functional Blocks

#### Hardware Integration
- **Teensy 3.6 microcontroller**
- **Rotary Encoder** (for navigating/selecting values and actions, with click/double/triple click detection)
- **0.96" OLED Display** (for menus, timers, feedback, and prompts)
- **Onboard microSD Card** (for file storage and plug-and-play data transfer)
- **Serial Interface** (connects to TX pin of any microcontroller for sensor mode)

#### Firmware/Embedded Software (Teensy)
- **User Interface**
  - OLED displays menus, prompts, timers, confirmations.
  - Rotary encoder handles navigation, value selection, and click actions.
- **File System Handling**
  - Read/write CSV or similar files to the onboard microSD card.
  - Support for file append, overwrite, and new file creation.
- **Serial Data Logging (Sensor Mode)**
  - Listen to incoming data from a designated UART/serial pin.
  - Capture and log serial data in real time to the microSD card.
  - Allow configurable baud rate and protocol if needed.
- **Basic Mode Data Logging**
  - Prompt user for manual data input via rotary encoder.
  - Log values at user-defined intervals and structure.
- **State Machine**
  - Idle, configuration, logging, confirmation, error handling, abort.

#### Python Host Script (Stored on microSD Card)
- **User Interaction**
  - Select or create data files for logging.
  - Configure logging parameters (mode, interval, file format, etc.).
- **File Management**
  - Ensure file consistency and update status.
  - Prompt user to confirm file sync or retrieve file if issues occur.

---

### Detailed Task List

#### Hardware Bring-Up
- Assemble Teensy 3.6, rotary encoder, OLED, and microSD interface.
- Test rotary encoder with click, double, triple click detection.
- Test OLED menu system.
- Validate microSD card read/write from Teensy.
- Test serial data capture from external TX source.

#### Teensy Firmware Development
- **File System**: Implement microSD card mounting, file creation, append, and update.
- **UI State Machine**: Build menu system for configuration, logging, confirmation, error, abort.
- **Rotary Encoder**: Implement value selection, click/double/triple click logic.
- **OLED Display**: Develop user prompts, row/column selection, timer display, and status messages.
- **Serial Logging (Sensor Mode)**: Implement data capture from TX pin, configurable baud rate, and error handling.
- **Basic Mode Logging**: User-driven data entry with timed or event-based logging.

#### Python Host Script Development
- **User Prompts**: File selection/creation, configuration (mode selection, interval, etc.).
- **Data Logging**: Option to log data directly on computer or via DataBuddy device.
- **File Sync**: Monitor file updates, confirm sync status.
- **Error Handling**: Provide user options if sync fails (manual retrieval, retry, etc.).

#### Integration & Testing
- End-to-end test: Python script → configuration → DataBuddy logging → file sync.
- microSD card: validate file updates between DataBuddy and computer.
- Serial: test data capture from various microcontroller TX sources.
- Rotary encoder: ensure reliable input, click detection.
- OLED: check for clear feedback and user guidance.

#### Documentation
- System overview and quick-start instructions.
- Hardware assembly guide.
- Firmware and Python script usage.
- Troubleshooting and FAQ.

#### Future/Optional Extensions
- Add sensor modules (e.g., thermistors, ADCs, digital sensors).
- Expand file format support (JSON, binary, etc.).
- Add BLE or WiFi for wireless sync.
- Data visualization on OLED or via Python script.
- Web dashboard for remote monitoring.

---


---

Let me know if you want to break any of these down further or need code samples for specific tasks!
