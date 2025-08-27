#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Encoder.h>
#include <SD.h>

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

#define ENCODER_DT 2
#define ENCODER_CLK 3
#define ENCODER_SW 4
Encoder knob(ENCODER_DT, ENCODER_CLK);

enum State {
  MENU,
  CONFIG_ROWS,
  CONFIG_COLS,
  CONFIG_TIMER,
  CONFIG_BEGIN,
  LOG_WAIT,
  LOG_ENTRY,
  LOG_DONE,
  CONFIRM_UNDO,
  SENSOR_MODE,
  SENSOR_CONFIG_TIME,
  SENSOR_WAIT_START,
  SENSOR_RECORDING,
  SENSOR_DONE,
  SENSOR_NOTAVAIL,
  SD_ERROR
};
State state = MENU;

// Menu data
const char* menuItems[] = { "Basic mode", "Sensor mode" };
const int menuLength = sizeof(menuItems) / sizeof(menuItems[0]);
int menuIndex = 0;

// Config values
int numRows = 4;
int numCols = 1;
float timeBetweenRows = 2.5; // minutes, float

// Logging
int currentRow = 0;
unsigned long logStartTime = 0;
unsigned long logWaitMillis = 0;
float manualValues[10];  // up to 10 columns
int editingCol = 0;
bool valueEntryActive = false;
bool justLogged = false;

// SD card
bool sdAvailable = false;
File logFile;
const char* logFilename = "datalog.csv";

// Encoder
long encoderPos = 0;
long lastEncoderPos = 0;
unsigned long lastEncoderChange = 0;
unsigned long encoderSpeed = 0;

// ---- SENSOR MODE VARS ----
unsigned long sensorDuration = 10000;   // ms
unsigned long sensorStartTime = 0;
bool sensorLoggingActive = false;
int sensorRow = 0;
bool sensorHeaderWritten = false;
bool sensorModeActive = false;
String lastSensorLine = "";

// OLED helpers
void printCentered(const char* text, int y, int tsize) {
  display.setTextSize(tsize);
  int16_t x1, y1; uint16_t w, h;
  display.getTextBounds((char*)text, 0, y, &x1, &y1, &w, &h);
  int xpos = (SCREEN_WIDTH - w) / 2;
  display.setCursor(xpos, y);
  display.print(text);
}

void printMenuItem(const char* text, int selected) {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  printCentered(text, 18, 2);
  display.setTextSize(1);
  if (selected > 0) {
    display.setCursor(SCREEN_WIDTH/2-3, 2);
    display.print("^");
  }
  if (selected < menuLength-1) {
    display.setCursor(SCREEN_WIDTH/2-3, SCREEN_HEIGHT-10);
    display.print("v");
  }
  display.display();
}

// Helper for column names
void buildCSVHeader(char* header, int numCols) {
  header[0] = 0;
  for (int i = 0; i < numCols; i++) {
    strcat(header, "val");
    char colNum[4];
    sprintf(colNum, "%d", i + 1);
    strcat(header, colNum);
    if (i < numCols - 1) strcat(header, ",");
  }
}

// Encoder step size for smooth 1-100 entry
float getStepSize(unsigned long speedMillis) {
  if (speedMillis < 80) return 2.0;
  if (speedMillis < 250) return 0.5;
  if (speedMillis < 600) return 0.1;
  return 0.01;
}

void setup() {
  pinMode(ENCODER_SW, INPUT_PULLUP);
  Serial.begin(9600);
  while (!Serial);

  Serial1.begin(115200); // For sensor mode RX from ESP32-C6

  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println(F("SSD1306 allocation failed"));
    while (1);
  }
  display.clearDisplay();
  display.display();

  Serial.println("Initializing SD card (Teensy 3.6 onboard slot)...");
  sdAvailable = SD.begin(BUILTIN_SDCARD);
  if (!sdAvailable) {
    Serial.println("SD card initialization failed or no card present!");
  } else {
    Serial.println("SD card initialized and available.");
  }
}

void loop() {
  switch (state) {
    case MENU:
      menuScreen();
      break;
    case CONFIG_ROWS:
      configRows();
      break;
    case CONFIG_COLS:
      configCols();
      break;
    case CONFIG_TIMER:
      configTimer();
      break;
    case CONFIG_BEGIN:
      configBegin();
      break;
    case LOG_WAIT:
      logWait();
      break;
    case LOG_ENTRY:
      logEntry();
      break;
    case LOG_DONE:
      logDone();
      break;
    case CONFIRM_UNDO:
      confirmUndo();
      break;
    case SENSOR_MODE:
      state = SENSOR_CONFIG_TIME;
      break;
    case SENSOR_CONFIG_TIME:
      sensorModeConfigTime();
      break;
    case SENSOR_WAIT_START:
      sensorModeWaitStart();
      break;
    case SENSOR_RECORDING:
      sensorModeRecording();
      break;
    case SENSOR_DONE:
      sensorModeDone();
      break;
    case SENSOR_NOTAVAIL:
      sensorNotAvailable();
      break;
    case SD_ERROR:
      display.clearDisplay();
      display.setTextSize(2);
      display.setCursor(0, 0);
      display.print("SD Error!");
      display.setTextSize(1);
      display.setCursor(0, 32);
      display.print("Insert SD card");
      display.display();
      delay(2000);
      state = MENU;
      break;
  }

  // ---- BEGIN DEBUG BLOCK - REMOVE WHEN NOT NEEDED ----
  // This block will print whatever is received on Serial1 (pin 0) to USB Serial Monitor
  // You can safely delete these lines when you're done testing!
  if (Serial1.available()) {
    String debugLine = Serial1.readStringUntil('\n');
    debugLine.trim();
    if (debugLine.length() > 0) {
      Serial.print("[DEBUG] Serial1 RX: ");
      Serial.println(debugLine);
    }
  }
  // ---- END DEBUG BLOCK ----
}

// ---- MENU SCREEN ----
void menuScreen() {
  long newPos = knob.read();
  if (newPos != lastEncoderPos) {
    menuIndex = constrain(newPos, 0, menuLength - 1);
    lastEncoderPos = newPos;
  }
  printMenuItem(menuItems[menuIndex], menuIndex);
  if (buttonPressed()) {
    if (menuIndex == 0) {
      state = CONFIG_ROWS;
      encoderPos = numRows;
      knob.write(encoderPos);
    } else if (menuIndex == 1) {
      state = SENSOR_MODE;
    }
  }
}

// ---- CONFIGURATION ----
void configRows() {
  long newPos = knob.read();
  if (newPos != lastEncoderPos) {
    numRows = constrain(newPos, 1, 100);
    lastEncoderPos = newPos;
  }
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  printCentered("Set rows:", 0, 2);
  printCentered(String(numRows).c_str(), 32, 2);
  display.display();
  if (buttonPressed()) {
    state = CONFIG_COLS;
    encoderPos = numCols;
    knob.write(encoderPos);
  }
}

void configCols() {
  long newPos = knob.read();
  if (newPos != lastEncoderPos) {
    numCols = constrain(newPos, 1, 10); // let's cap at 10 for OLED clarity
    lastEncoderPos = newPos;
  }
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  printCentered("Set columns:", 0, 2);
  printCentered(String(numCols).c_str(), 32, 2);
  display.display();
  if (buttonPressed()) {
    state = CONFIG_TIMER;
    encoderPos = (long)(timeBetweenRows * 100);
    knob.write(encoderPos);
  }
}

void configTimer() {
  long newPos = knob.read();
  if (newPos != lastEncoderPos) {
    float val = constrain(newPos, 1, 10000); // up to 100 min
    timeBetweenRows = val / 100.0;
    lastEncoderPos = newPos;
  }
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  printCentered("Time/log (min):", 0, 2);
  printCentered(String(timeBetweenRows, 2).c_str(), 32, 2);
  display.display();
  if (buttonPressed()) {
    state = CONFIG_BEGIN;
  }
}

void configBegin() {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  printCentered(("rows: " + String(numRows) + " cols: " + String(numCols)).c_str(), 0, 1);
  printCentered(("interval: " + String(timeBetweenRows,2) + " min").c_str(), 16, 1);
  display.setTextSize(2);
  printCentered("Press to begin!", 32, 2);
  display.display();

  if (buttonPressed()) {
    if (!sdAvailable) {
      Serial.println("SD card not available when entering logging state.");
      state = SD_ERROR;
    } else {
      logFile = SD.open(logFilename, FILE_WRITE);
      if (logFile) {
        // Write header: column names
        char header[128] = "";
        buildCSVHeader(header, numCols);
        logFile.print(header); logFile.print("\n");
        logFile.print("interval: "); logFile.print(timeBetweenRows, 2); logFile.print(" min\n");
        logFile.flush();
      } else {
        Serial.println("Failed to open log file for writing.");
        state = SD_ERROR;
      }
      currentRow = 1;
      logWaitMillis = (unsigned long)(timeBetweenRows * 60000); // min to ms
      logStartTime = millis();
      editingCol = 0;
      valueEntryActive = false;
      justLogged = false;
      state = LOG_WAIT;
    }
  }
}

// ---- LOGGING ----
void logWait() {
  unsigned long now = millis();
  unsigned long elapsed = now - logStartTime;
  unsigned long remain = (elapsed < logWaitMillis) ? (logWaitMillis - elapsed) : 0;

  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(2);
  printCentered("Next log in:", 0, 2);
  printCentered((String(remain / 60000.0,2) + " min").c_str(), 32, 2);
  display.setTextSize(1);
  printCentered(("Progress: " + String(currentRow) + "/" + String(numRows)).c_str(), 56, 1);
  display.display();

  if (buttonPressed()) {
    for (int i = 0; i < numCols; i++) manualValues[i] = 0.0;
    editingCol = 0;
    valueEntryActive = true;
    encoderPos = 0;
    knob.write(encoderPos);
    state = LOG_ENTRY;
    justLogged = false;
    return;
  }
  if (remain == 0) {
    for (int i = 0; i < numCols; i++) manualValues[i] = 0.0;
    editingCol = 0;
    valueEntryActive = true;
    encoderPos = 0;
    knob.write(encoderPos);
    state = LOG_ENTRY;
    justLogged = false;
    return;
  }
}

// ---- Value Entry for Each Column ----
void logEntry() {
  if (valueEntryActive) {
    long newPos = knob.read();
    unsigned long now = millis();
    if (newPos != lastEncoderPos) {
      encoderSpeed = now - lastEncoderChange;
      lastEncoderChange = now;
      lastEncoderPos = newPos;
      float stepSize = getStepSize(encoderSpeed);
      manualValues[editingCol] += (newPos > encoderPos) ? stepSize : -stepSize;
      manualValues[editingCol] = constrain(manualValues[editingCol], 0, 10000);
      encoderPos = newPos;
    }
    display.clearDisplay();
    display.setTextColor(SSD1306_WHITE);
    display.setTextSize(2);
    printCentered(("val" + String(editingCol+1) + ":").c_str(), 0, 2);
    printCentered(String(manualValues[editingCol],2).c_str(), 32, 2);
    display.setTextSize(1);
    if (editingCol < numCols - 1) {
      printCentered("Press: next col", 56, 1);
    } else {
      printCentered("Press: log entry", 56, 1);
    }
    display.display();

    if (buttonPressed()) {
      if (editingCol < numCols - 1) {
        editingCol++;
        encoderPos = 0;
        knob.write(encoderPos);
      } else {
        valueEntryActive = false;
      }
      return;
    }
    return;
  }

  // All values entered, log them
  if (!valueEntryActive) {
    if (sdAvailable && logFile) {
      for (int i = 0; i < numCols; i++) {
        logFile.print(manualValues[i], 2);
        if (i < numCols - 1) logFile.print(",");
      }
      logFile.print("\n");
      logFile.flush();
      justLogged = true;
    } else {
      Serial.println("SD or logFile not available on log attempt.");
      state = SD_ERROR;
    }
    display.clearDisplay();
    display.setTextColor(SSD1306_WHITE);
    display.setTextSize(2);
    printCentered(("Logged! " + String(currentRow) + "/" + String(numRows)).c_str(), 16, 2);
    display.display();
    delay(1000);

    currentRow++;
    if (currentRow > numRows) {
      state = LOG_DONE;
    } else {
      logStartTime = millis();
      valueEntryActive = false;
      editingCol = 0;
      for (int i = 0; i < numCols; i++) manualValues[i] = 0.0;
      state = LOG_WAIT;
    }
    return;
  }

  // Double-tap to go back, only if just logged previous entry
  if (justLogged && doubleClickDetected()) {
    state = CONFIRM_UNDO;
    return;
  }
}

// ---- CONFIRM UNDO ----
void confirmUndo() {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(2);
  printCentered("Undo last?", 8, 2);
  display.setTextSize(1);
  printCentered("Deletes last entry.", 40, 1);
  printCentered("Press: Yes", 56, 1);
  display.display();

  if (buttonPressed()) {
    removeLastLogEntry();
    currentRow = max(1, currentRow - 1);
    for (int i = 0; i < numCols; i++) manualValues[i] = 0.0;
    editingCol = 0;
    valueEntryActive = true;
    encoderPos = 0;
    knob.write(encoderPos);
    justLogged = false;
    state = LOG_ENTRY;
    return;
  }
  if (doubleClickDetected()) {
    justLogged = false;
    state = LOG_WAIT;
    logStartTime = millis();
    return;
  }
}

void logDone() {
  if (sdAvailable && logFile) {
    logFile.close();
  }
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(2);
  printCentered("Logging", 8, 2);
  printCentered("Complete!", 32, 2);
  display.setTextSize(1);
  printCentered("Returning to menu...", 56, 1);
  display.display();
  delay(2000);
  state = MENU;
  encoderPos = menuIndex;
  knob.write(encoderPos);
}

// ---- SENSOR MODE ----
void sensorModeConfigTime() {
  long newPos = knob.read();
  if (newPos != lastEncoderPos) {
    sensorDuration = constrain(newPos, 1, 600) * 1000; // seconds to ms
    lastEncoderPos = newPos;
  }
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  printCentered("Duration (s):", 0, 2);
  printCentered(String(sensorDuration/1000).c_str(), 32, 2);
  display.display();
  if (buttonPressed()) {
    state = SENSOR_WAIT_START;
  }
}

void sensorModeWaitStart() {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  printCentered("Press to start", 16, 2);
  display.display();
  if (buttonPressed()) {
    // Open log file, prepare for logging
    if (!sdAvailable) {
      state = SD_ERROR;
      return;
    }
    logFile = SD.open(logFilename, FILE_WRITE);
    sensorStartTime = millis();
    sensorLoggingActive = true;
    sensorRow = 0;
    sensorHeaderWritten = false;
    sensorModeActive = true;
    lastSensorLine = "";
    state = SENSOR_RECORDING;
  }
}

void sensorModeRecording() {
  if (!sensorLoggingActive) return;
  unsigned long now = millis();
  unsigned long elapsed = now - sensorStartTime;

  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(2);
  printCentered("Recording...", 0, 2);
  display.setTextSize(1);
  printCentered(("Time left: " + String((sensorDuration-elapsed)/1000) + "s").c_str(), 32, 1);
  printCentered(("Logged: " + String(sensorRow)).c_str(), 48, 1);
  // Display the latest temp readings at the bottom
  if (lastSensorLine.length() > 0) {
    display.setCursor(0, 56);
    display.print("Last: ");
    display.print(lastSensorLine.c_str());
  }
  display.display();

  if (elapsed >= sensorDuration) {
    sensorLoggingActive = false;
    sensorModeActive = false;
    if (logFile) logFile.close();
    state = SENSOR_DONE;
    return;
  }
  while (Serial1.available()) {
    String line = Serial1.readStringUntil('\n');
    line.trim();
    if (line.length() > 0) {
      // Write header on first line
      if (!sensorHeaderWritten) {
        logFile.print("timestamp,temp1,temp2\n");
        sensorHeaderWritten = true;
      }
      logFile.print(String(millis())); // timestamp
      logFile.print(",");
      logFile.print(line); // temp values from ESP32-C6, e.g. "23.5,24.1"
      logFile.print("\n");
      logFile.flush();
      sensorRow++;

      // Store last received line for display
      lastSensorLine = line;
    }
  }
}

void sensorModeDone() {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(2);
  printCentered("Sensor log done", 16, 2);
  display.display();
  delay(2000);
  state = MENU;
  sensorModeActive = false;
  encoderPos = menuIndex;
  knob.write(encoderPos);
}

void sensorNotAvailable() {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(2);
  printCentered("Not available", 0, 2);
  printCentered("yet!", 28, 2);
  display.setTextSize(1);
  printCentered("Double-click to exit", 54, 1);
  display.display();
  if (doubleClickDetected()) {
    state = MENU;
    encoderPos = menuIndex;
    knob.write(encoderPos);
  }
}

// ---- Helper Functions ----
bool buttonPressed() {
  static bool prev = HIGH;
  bool cur = digitalRead(ENCODER_SW);
  if (prev == HIGH && cur == LOW) {
    prev = cur;
    delay(200); // debounce
    return true;
  }
  prev = cur;
  return false;
}
bool doubleClickDetected() {
  static unsigned long lastTime = 0;
  static int clicks = 0;
  bool cur = digitalRead(ENCODER_SW);
  if (cur == LOW) {
    if (millis() - lastTime > 200) { // debounce
      clicks++;
      lastTime = millis();
    }
    if (clicks == 2 && millis() - lastTime < 600) {
      clicks = 0;
      return true;
    }
  } else {
    if (millis() - lastTime > 600) clicks = 0;
  }
  return false;
}

// Remove last log entry (from CSV file)
void removeLastLogEntry() {
  File f = SD.open(logFilename, FILE_READ);
  if (!f) return;
  String newContent = "";
  int nlines = 0;
  while (f.available()) {
    String line = f.readStringUntil('\n');
    if (line.length() > 0) {
      nlines++;
      newContent += line + "\n";
    }
  }
  f.close();
  int lastLogIdx = newContent.lastIndexOf('\n', newContent.length()-2);
  if (lastLogIdx > 0) {
    int headerEnd = newContent.indexOf('\n', 0);
    headerEnd = newContent.indexOf('\n', headerEnd+1);
    newContent = newContent.substring(0, lastLogIdx+1);
    if (headerEnd > 0 && lastLogIdx < headerEnd) {
      newContent = newContent;
    }
  }
  SD.remove(logFilename);
  File fw = SD.open(logFilename, FILE_WRITE);
  if (!fw) return;
  fw.print(newContent);
  fw.close();
}
