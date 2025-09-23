#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Encoder.h>
#include <SD.h>
#include <EEPROM.h>

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

#define ENCODER_DT 2
#define ENCODER_CLK 3
#define ENCODER_SW 4
Encoder knob(ENCODER_DT, ENCODER_CLK);

// --- BATTERY MONITOR ---
#define BATTERY_PIN 23
#define BAT_R1 2700.0
#define BAT_R2 10000.0
#define BAT_VOLTAGE_FULL 4.2
#define BAT_VOLTAGE_EMPTY 3.0
#define BATTERY_SAMPLES 10
#define BATTERY_UPDATE_THRESHOLD 2

float batterySamples[BATTERY_SAMPLES];
int batterySampleIndex = 0;
bool batteryBufferFilled = false;
int lastDisplayedPercent = -1;

// --- Check Test Mode Data Structures ---
#define MAX_VALVES 4
#define MAX_TESTS_PER_VALVE 3
#define MAX_LABELS 2

// Test parameters
struct TestConfig {
  String testType;
  int columns;
  int rows;
  float intervalMin;
  String labels[MAX_LABELS];
};

struct ValveConfig {
  String valveName;
  TestConfig tests[MAX_TESTS_PER_VALVE];
  int numTests;
};

ValveConfig valveTable[MAX_VALVES];
int numValves = 0;

// ---- MENU ----
const char* menuItems[] = { "Check Test Mode", "Basic mode", "Sensor mode" };
const int menuLength = sizeof(menuItems) / sizeof(menuItems[0]);
int menuIndex = 0;

// --- Config values (for basic mode) ---
int numRows = 4;
int numCols = 1;
float timeBetweenRows = 2.5;
String columnLabels[10];

int currentRow = 0;
unsigned long logStartTime = 0;
unsigned long logWaitMillis = 0;
float manualValues[10];
float lastManualValues[10];
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
long menuEncoderBase = 0;
int menuEncoderDelay = 2;

// ---- SENSOR MODE VARS ----
unsigned long sensorDuration = 10000;
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

// --- Winky face intro ---
void showIntroScreen() {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(3);

  // EEPROM[0] stores which face to use: 0=":)", 1=";)", 2=":)"
  uint8_t faceMode = EEPROM.read(0) % 3;
  const char* faces_compact[3] = {":-)", ";-)", ":-)"};

  printCentered(faces_compact[faceMode], 22, 3); // moved up, less space
  display.setTextSize(1);
  printCentered("Welcome to DataBuddy v2", 48, 1);
  display.display();
  delay(2000);

  EEPROM.write(0, (faceMode + 1) % 3);
}

void buildCSVHeader(char* header, int numCols, TestConfig* t) {
  header[0] = 0;
  for (int i = 0; i < numCols; i++) {
    if (t && i < MAX_LABELS && t->labels[i].length() > 0) {
      strcat(header, t->labels[i].c_str());
    } else {
      strcat(header, "val");
      char colNum[4];
      sprintf(colNum, "%d", i + 1);
      strcat(header, colNum);
    }
    if (i < numCols - 1) strcat(header, ",");
  }
}

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
  SD_ERROR,
  CHECK_TEST_SELECT,
  CHECK_TEST_INIT,
  CHECK_TEST_WAIT,
  CHECK_TEST_ENTRY,
  CHECK_TEST_DONE
};
State state = MENU;

// ---- Battery Functions ----
float readRawBatteryVoltage() {
  int raw = analogRead(BATTERY_PIN);
  float vRef = 3.3;
  float vDiv = (raw / 1023.0) * vRef;
  float vBat = vDiv * ((BAT_R1 + BAT_R2) / BAT_R2);
  return vBat;
}

void updateBatteryBuffer() {
  float v = readRawBatteryVoltage();
  batterySamples[batterySampleIndex++] = v;
  if (batterySampleIndex >= BATTERY_SAMPLES) {
    batterySampleIndex = 0;
    batteryBufferFilled = true;
  }
}

float getSmoothedBatteryVoltage() {
  int count = batteryBufferFilled ? BATTERY_SAMPLES : batterySampleIndex;
  if (count == 0) return 0;
  float sum = 0;
  for (int i = 0; i < count; i++) sum += batterySamples[i];
  return sum / count;
}

int batteryPercent() {
  float v = getSmoothedBatteryVoltage();
  if (v < 0.5 || v > 5.5) return -1;
  if (v >= BAT_VOLTAGE_FULL) return 100;
  if (v <= BAT_VOLTAGE_EMPTY) return 0;
  return (int)(((v - BAT_VOLTAGE_EMPTY) / (BAT_VOLTAGE_FULL - BAT_VOLTAGE_EMPTY)) * 100.0);
}

void drawBatteryIndicator() {
  int percent = batteryPercent();
  if (lastDisplayedPercent < 0 || (percent >= 0 && abs(percent - lastDisplayedPercent) > BATTERY_UPDATE_THRESHOLD)) {
    lastDisplayedPercent = percent;
  }
  int bx = SCREEN_WIDTH - 24;
  int by = 0;
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.drawRect(bx, by, 20, 8, SSD1306_WHITE);
  display.fillRect(bx + 20, by + 2, 2, 4, SSD1306_WHITE);
  int fillWidth = 2;
  if (lastDisplayedPercent >= 0)
    fillWidth = map(lastDisplayedPercent, 0, 100, 2, 18);
  display.fillRect(bx + 1, by + 1, fillWidth, 6, SSD1306_WHITE);
  display.setCursor(bx - 18, by);
  if (lastDisplayedPercent < 0) {
    display.print("--%");
  } else {
    display.print(String(lastDisplayedPercent) + "%");
  }
}

// ---- Helper Functions ----
bool buttonPressed() {
  static bool prev = HIGH;
  bool cur = digitalRead(ENCODER_SW);
  if (prev == HIGH && cur == LOW) {
    prev = cur;
    delay(200);
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
    if (millis() - lastTime > 200) {
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

// ---- Valve Test Config ----
void readCheckTestConfig() {
  numValves = 0;
  File f = SD.open("check_test_mode.txt", FILE_READ);
  if (!f) return;
  while (f.available() && numValves < MAX_VALVES) {
    String line = f.readStringUntil('\n');
    line.trim();
    if (line.length() == 0 || line.charAt(0) == '#') continue;
    int idx1 = line.indexOf(',');
    int idx2 = line.indexOf(',', idx1+1);
    int idx3 = line.indexOf(',', idx2+1);
    int idx4 = line.indexOf(',', idx3+1);
    int idx5 = line.indexOf(',', idx4+1);
    if (idx1 < 0 || idx2 < 0 || idx3 < 0 || idx4 < 0) continue;
    String valve = line.substring(0, idx1);
    String testType = line.substring(idx1+1, idx2);
    int cols = line.substring(idx2+1, idx3).toInt();
    int rows = line.substring(idx3+1, idx4).toInt();
    float interval = line.substring(idx4+1, idx5 > 0 ? idx5 : line.length()).toFloat();

    // Find existing valve
    int valveIdx = -1;
    for (int v=0; v<numValves; v++)
      if (valveTable[v].valveName == valve) valveIdx = v;
    if (valveIdx == -1) {
      valveIdx = numValves++;
      valveTable[valveIdx].valveName = valve;
      valveTable[valveIdx].numTests = 0;
    }
    int tIdx = valveTable[valveIdx].numTests;
    if (tIdx >= MAX_TESTS_PER_VALVE) continue;
    valveTable[valveIdx].tests[tIdx].testType = testType;
    valveTable[valveIdx].tests[tIdx].columns = cols;
    valveTable[valveIdx].tests[tIdx].rows = rows;
    valveTable[valveIdx].tests[tIdx].intervalMin = interval;
    // Optional label 1
    if (idx5 > 0) {
      int idx6 = line.indexOf(',', idx5+1);
      valveTable[valveIdx].tests[tIdx].labels[0] = line.substring(idx5+1, idx6 > 0 ? idx6 : line.length());
      if (idx6 > 0) valveTable[valveIdx].tests[tIdx].labels[1] = line.substring(idx6+1);
    } else {
      valveTable[valveIdx].tests[tIdx].labels[0] = "";
      valveTable[valveIdx].tests[tIdx].labels[1] = "";
    }
    valveTable[valveIdx].numTests++;
  }
  f.close();
}

// ---- MENU SCREEN ----
void printMenuItem(const char* text, int selected) {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  printCentered(text, 18, 2);
  display.setTextSize(1);
  drawBatteryIndicator();
  display.display();
}

void menuScreen() {
  long newPos = knob.read();
  int delta = (newPos - menuEncoderBase) / menuEncoderDelay;
  if (delta != 0) {
    menuIndex = (menuIndex + delta + menuLength) % menuLength;
    menuEncoderBase += delta * menuEncoderDelay;
  }
  printMenuItem(menuItems[menuIndex], menuIndex);
  if (buttonPressed()) {
    if (menuIndex == 0) {
      if (!sdAvailable) {
        state = SD_ERROR;
        return;
      }
      readCheckTestConfig();
      if (numValves > 0) {
        encoderPos = 0;
        lastEncoderPos = 0;
        menuEncoderBase = 0;
        state = CHECK_TEST_SELECT;
      } else {
        display.clearDisplay();
        display.setTextSize(2);
        printCentered("No tests found", 16, 2);
        drawBatteryIndicator();
        display.display();
        delay(2000);
        state = MENU;
      }
    } else if (menuIndex == 1) {
      state = CONFIG_ROWS;
      encoderPos = numRows;
      knob.write(encoderPos);
    } else if (menuIndex == 2) {
      state = SENSOR_MODE;
    }
  }
}

// ---- CONFIGURATION ----
// ... [basic mode unchanged from your last working version] ...

// ---- Check Test Mode ----
int selectedValveIdx = 0;
int selectedTestIdx = 0;
TestConfig* currentTest = nullptr;
ValveConfig* currentValve = nullptr;

void checkTestSelectScreen() {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);

  long newPos = knob.read();
  int delta = (newPos - menuEncoderBase) / menuEncoderDelay;
  if (delta != 0) {
    selectedValveIdx = (selectedValveIdx + delta + numValves) % numValves;
    menuEncoderBase += delta * menuEncoderDelay;
  }

  display.setTextSize(2);
  printCentered(valveTable[selectedValveIdx].valveName.c_str(), 18, 2);
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.print("Use knob to select valve.");
  display.setCursor(0, SCREEN_HEIGHT-10);
  display.print("Press: Start");
  drawBatteryIndicator();
  display.display();

  if (buttonPressed()) {
    currentValve = &valveTable[selectedValveIdx];
    selectedTestIdx = 0;
    state = CHECK_TEST_INIT;
  }
}

void checkTestInit() {
  if (selectedTestIdx >= currentValve->numTests) {
    state = CHECK_TEST_DONE;
    return;
  }
  currentTest = &currentValve->tests[selectedTestIdx];
  numRows = currentTest->rows;
  numCols = currentTest->columns;
  timeBetweenRows = currentTest->intervalMin;
  for (int i=0; i<numCols; i++) {
    lastManualValues[i] = 0.0;
    manualValues[i] = 0.0;
    columnLabels[i] = currentTest->labels[i].length() > 0 ? currentTest->labels[i] : "val"+String(i+1);
  }
  currentRow = 1;
  justLogged = false;
  editingCol = 0;
  valueEntryActive = false;
  logWaitMillis = (unsigned long)(timeBetweenRows * 60000);
  logStartTime = millis();

  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  printCentered(("Valve: " + currentValve->valveName).c_str(), 0, 1);
  printCentered(("Test: " + currentTest->testType).c_str(), 16, 1);
  printCentered(("Rows: " + String(numRows) + " Cols: " + String(numCols)).c_str(), 32, 1);
  printCentered(("Interval: " + String(timeBetweenRows,2) + " min").c_str(), 48, 1);
  display.setTextSize(2);
  printCentered("Press to begin!", 56, 2);
  drawBatteryIndicator();
  display.display();

  if (buttonPressed()) {
    if (!sdAvailable) {
      state = SD_ERROR;
      return;
    }
    logFile = SD.open(logFilename, FILE_WRITE);
    if (logFile) {
      char header[128] = "";
      buildCSVHeader(header, numCols, currentTest);
      logFile.print("# Valve: "); logFile.print(currentValve->valveName); logFile.print("\n");
      logFile.print("# Test: "); logFile.print(currentTest->testType); logFile.print("\n");
      logFile.print(header); logFile.print("\n");
      logFile.print("interval: "); logFile.print(timeBetweenRows, 2); logFile.print(" min\n");
      logFile.flush();
    } else {
      state = SD_ERROR;
      return;
    }
    for (int i=0; i<numCols; i++) manualValues[i] = lastManualValues[i];
    state = CHECK_TEST_WAIT;
  }
}

void checkTestWait() {
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
  drawBatteryIndicator();
  display.display();

  if (buttonPressed()) {
    for (int i = 0; i < numCols; i++) manualValues[i] = lastManualValues[i];
    editingCol = 0;
    valueEntryActive = true;
    encoderPos = (long)manualValues[editingCol] * 10;
    knob.write(encoderPos);
    state = CHECK_TEST_ENTRY;
    justLogged = false;
    return;
  }
  if (remain == 0) {
    for (int i = 0; i < numCols; i++) manualValues[i] = lastManualValues[i];
    editingCol = 0;
    valueEntryActive = true;
    encoderPos = (long)manualValues[editingCol] * 10;
    knob.write(encoderPos);
    state = CHECK_TEST_ENTRY;
    justLogged = false;
    return;
  }
}

void checkTestEntry() {
  if (valueEntryActive) {
    long newPos = knob.read();
    if (newPos != lastEncoderPos) {
      float stepSize = 0.1;
      manualValues[editingCol] += (newPos > lastEncoderPos) ? stepSize : -stepSize;
      manualValues[editingCol] = constrain(manualValues[editingCol], 0, 10000);
      lastEncoderPos = newPos;
    }
    display.clearDisplay();
    display.setTextColor(SSD1306_WHITE);
    display.setTextSize(2);
    printCentered((columnLabels[editingCol] + ":").c_str(), 0, 2);
    printCentered(String(manualValues[editingCol],2).c_str(), 32, 2);
    display.setTextSize(1);
    if (editingCol < numCols - 1) {
      printCentered("Press: next col", 56, 1);
    } else {
      printCentered("Press: log entry", 56, 1);
    }
    drawBatteryIndicator();
    display.display();

    if (buttonPressed()) {
      lastManualValues[editingCol] = manualValues[editingCol];
      if (editingCol < numCols - 1) {
        editingCol++;
        encoderPos = (long)manualValues[editingCol] * 10;
        knob.write(encoderPos);
      } else {
        valueEntryActive = false;
      }
      return;
    }
    return;
  }

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
      state = SD_ERROR;
    }
    display.clearDisplay();
    display.setTextColor(SSD1306_WHITE);
    display.setTextSize(2);
    printCentered(("Logged! " + String(currentRow) + "/" + String(numRows)).c_str(), 16, 2);
    drawBatteryIndicator();
    display.display();
    delay(1000);

    currentRow++;
    if (currentRow > numRows) {
      selectedTestIdx++;
      if (logFile) logFile.close();
      state = CHECK_TEST_INIT;
    } else {
      logStartTime = millis();
      valueEntryActive = false;
      editingCol = 0;
      for (int i = 0; i < numCols; i++) manualValues[i] = lastManualValues[i];
      state = CHECK_TEST_WAIT;
    }
    return;
  }

  if (justLogged && doubleClickDetected()) {
    state = CONFIRM_UNDO;
    return;
  }
}

void checkTestDone() {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(2);
  printCentered("Valve tests", 8, 2);
  printCentered("Complete!", 32, 2);
  display.setTextSize(1);
  printCentered("Returning to menu...", 56, 1);
  drawBatteryIndicator();
  display.display();
  delay(2000);
  state = MENU;
  encoderPos = menuIndex;
  knob.write(encoderPos);
}

// ---- Sensor Mode ----
// [sensor mode unchanged, use your last working version]

// ---- Setup and Loop ----
void setup() {
  pinMode(ENCODER_SW, INPUT_PULLUP);
  Serial.begin(9600);
  Serial1.begin(115200);

  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println(F("SSD1306 allocation failed"));
    while (1);
  }
  display.clearDisplay();
  display.display();

  showIntroScreen();

  Serial.println("Initializing SD card (Teensy 3.6 onboard slot)...");
  sdAvailable = SD.begin(BUILTIN_SDCARD);
  if (!sdAvailable) {
    Serial.println("SD card initialization failed or no card present!");
  } else {
    Serial.println("SD card initialized and available.");
  }
  for(int i=0;i<BATTERY_SAMPLES;i++) updateBatteryBuffer();
  for(int i=0;i<10;i++) lastManualValues[i] = 0.0;
  encoderPos = knob.read();
  lastEncoderPos = encoderPos;
  menuEncoderBase = encoderPos;
}

void loop() {
  updateBatteryBuffer();
  switch (state) {
    case MENU: menuScreen(); break;
    case CONFIG_ROWS: /* unchanged */ break;
    case CONFIG_COLS: /* unchanged */ break;
    case CONFIG_TIMER: /* unchanged */ break;
    case CONFIG_BEGIN: /* unchanged */ break;
    case LOG_WAIT: /* unchanged */ break;
    case LOG_ENTRY: /* unchanged */ break;
    case LOG_DONE: /* unchanged */ break;
    case CONFIRM_UNDO: /* unchanged */ break;
    case SENSOR_MODE: state = SENSOR_CONFIG_TIME; break;
    case SENSOR_CONFIG_TIME: /* unchanged */ break;
    case SENSOR_WAIT_START: /* unchanged */ break;
    case SENSOR_RECORDING: /* unchanged */ break;
    case SENSOR_DONE: /* unchanged */ break;
    case SENSOR_NOTAVAIL: /* unchanged */ break;
    case SD_ERROR:
      display.clearDisplay();
      display.setTextSize(2);
      display.setCursor(0, 0);
      display.print("SD Error!");
      display.setTextSize(1);
      display.setCursor(0, 32);
      display.print("Insert SD card");
      drawBatteryIndicator();
      display.display();
      delay(2000);
      state = MENU;
      break;
    case CHECK_TEST_SELECT: checkTestSelectScreen(); break;
    case CHECK_TEST_INIT: checkTestInit(); break;
    case CHECK_TEST_WAIT: checkTestWait(); break;
    case CHECK_TEST_ENTRY: checkTestEntry(); break;
    case CHECK_TEST_DONE: checkTestDone(); break;
  }
}
