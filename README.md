# DataBuddy Project: Verbose Implementation Plan

## 1. **System Overview**

DataBuddy is an ESP32-based hardware and software platform for flexible, interactive data logging. The user interacts via a rotary encoder and OLED screen, and data is stored and transferred using a USB flash drive. A Python script on the drive offers configuration, file management, and an optional pairing process for BLE communication between the computer and DataBuddy.

---

## 2. **Major Functional Blocks**

### 2.1. **Hardware Integration**
- **ESP32C6 microcontroller**
- **Rotary Encoder** (for navigating/selecting values, double/triple click for special actions)
- **0.96" OLED Display** (for menu, timers, feedback, prompts)
- **USB Flash Drive** (for file storage, plug-and-play data transfer)
- **BLE Module** (built into ESP32 for wireless communication with host/computer)

### 2.2. **Firmware/Embedded Software (ESP32)**
- **User Interface**
  - OLED displays menus, prompts, timers, confirmations.
  - Rotary encoder handles navigation, value selection, and click actions (single, double, triple).
- **File System Handling**
  - Read/write CSV or similar files to USB drive.
  - Support for file append, overwrite, and new file creation.
- **Bluetooth Communication**
  - BLE server/client for file/data sync and configuration input from host computer.
  - Notify host on file updates or sync requests.
- **Data Logging Protocol**
  - Log values at user-defined intervals and structure.
  - Real-time updating of files.
  - Prompt user for cell input; handle abort/go-back actions.
- **State Machine**
  - Idle, configuration, logging, confirmation, error handling, abort, sync.

### 2.3. **Python Host Script (Stored on USB Drive)**
- **User Interaction**
  - Select or create data files for logging.
  - Configure number of rows, columns, time interval, starting cell.
- **Pairing & Protocol**
  - Optionally connect to DataBuddy via BLE.
  - If paired, prompt for rotary encoder settings and instruct user to return drive to DataBuddy.
  - If not paired, run data logging protocol entirely on the computer.
- **Data Sync**
  - Send configuration to DataBuddy via BLE.
  - Receive data updates and write to file in real-time (if possible).
- **File Management**
  - Ensure file consistency and update status.
  - Prompt user to confirm file sync or retrieve file manually if sync fails.

---

## 3. **Detailed Task List**

### 3.1. Hardware Bring-Up
- Assemble ESP32C6, rotary encoder, OLED, USB interface.
- Test rotary encoder with click, double, triple click detection.
- Test OLED menu system.
- Validate USB flash drive read/write from ESP32.

### 3.2. ESP32 Firmware Development
- **File System**: Implement USB flash drive mounting, file creation, append, and update.
- **UI State Machine**: Build menu system for configuration, logging, confirmation, error, abort.
- **Rotary Encoder**: Implement value selection, click/double/triple click logic.
- **OLED Display**: Develop user prompts, row/column selection, timer display, and status messages.
- **BLE Communication**: Set up BLE server on DataBuddy for config/data sync with Python host.
- **Logging Protocol**: Implement timed logging, cell input, auto-update of file, and special actions (go back/abort).

### 3.3. Python Host Script Development
- **User Prompts**: File selection/creation, configuration (rows, cols, interval, starting cell).
- **Pairing Logic**: BLE scan/connect to DataBuddy, rotary encoder calibration, prompt user to return drive.
- **Data Logging**: If unpaired, collect and log data directly on computer; if paired, send config to DataBuddy and monitor real-time updates.
- **File Sync**: Monitor BLE data updates, write changes to file, confirm sync status.
- **Error Handling**: Provide user options if sync fails (manual retrieval, retry, etc.).

### 3.4. Integration & Testing
- End-to-end test: Python script → BLE config → DataBuddy logging → file sync.
- USB drive: validate file updates between DataBuddy and computer.
- BLE: test connection robustness, data transfer, sync feedback.
- Rotary encoder: ensure reliable input, click detection.
- OLED: check for clear feedback and user guidance.

### 3.5. Documentation
- System overview and quick-start instructions.
- Hardware assembly guide.
- Firmware and Python script usage.
- Troubleshooting and FAQ.

### 3.6. Future/Optional Extensions
- Add sensor modules (e.g., thermistors).
- Expand BLE features (multi-device sync, OTA updates).
- Data visualization on OLED or via Python script.
- Advanced file formats (JSON, binary).
- Web dashboard for remote monitoring.

---

## 4. **Recommended File/Directory Structure**

```
DataBuddy/
├── firmware/           # ESP32 source code
│   ├── main.cpp
│   ├── rotary_encoder.cpp
│   ├── oled_display.cpp
│   ├── ble_server.cpp
│   └── file_system.cpp
├── host_script/        # Python host code
│   └── databuddy_host.py
├── docs/               # Documentation
│   └── README.md
│   └── hardware_setup.md
│   └── usage_guide.md
├── hardware/           # Schematics, BOM, etc.
│   └── schematic.pdf
├── test/               # Test scripts and logs
│   └── integration_test.md
└── .gitignore
```

---

## 5. **Action Steps (Short-Term)**

1. **Hardware prototyping** (ESP32C6 + rotary encoder + OLED + USB)
2. **Start ESP32 firmware skeleton** (UI, file system, BLE server, rotary logic)
3. **Develop initial Python script** (file management, configuration, BLE pairing logic)
4. **Test file sync and data logging between computer and DataBuddy**
5. **Iterate on user experience and robustness**

---

*Let me know if you want a more detailed breakdown for a specific section, or want to start with code templates for any of the components!*
