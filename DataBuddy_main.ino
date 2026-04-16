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

#define ENCODER_SW 2
#define ENCODER_DT 3
#define ENCODER_CLK 4
Encoder knob(ENCODER_DT, ENCODER_CLK);

// --- BATTERY MONITOR ---
#define BATTERY_PIN 23
#define BAT_R1 2700.0
#define BAT_R2 10000.0
#define BAT_VOLTAGE_FULL 4.2
#define BAT_VOLTAGE_EMPTY 3.0
#define BATTERY_SAMPLES 10
#define BATTERY_UPDATE_THRESHOLD 2
bool usingBattery = false;

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
const char* menuItems[] = { "Check Test Mode", "Basic mode", "Sensor mode", "Turbine Bench" };
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
  CHECK_TEST_DONE,
  TURBINE_IDLE,
  TURBINE_ZEROING,
  TURBINE_LOGGING,
  TURBINE_DONE,
  // Extended turbine bench workflow (v2)
  TURBINE_SELECT_FLOWMETER,
  TURBINE_CALIB_CHOICE,
  TURBINE_CALIB_SETFLOW,
  TURBINE_CALIB_ACQUIRE,
  TURBINE_CALIB_HIGHFLOW,
  TURBINE_SELECT_TURBINE,
  TURBINE_UNLISTED_ID,
  TURBINE_FIND_MIN_FLOW,
  TURBINE_SWEEPING,
  TURBINE_NEXT_FLOW,
  TURBINE_SAVING,
  TURBINE_ASK_ANOTHER
};
State state = MENU;

// ---- Turbine Bench ----
const int TURBINE_ZERO_SAMPLES = 20;
const unsigned long TURBINE_DISPLAY_INTERVAL_MS = 500;
const int TURBINE_FLUSH_INTERVAL_ROWS = 25;
// Turbine generator considered spinning once output exceeds this threshold
static const float TURBINE_MIN_VOLTAGE_V = 0.05f; // 50 mV
// Unit conversion constants
static const float GPM_TO_M3S = 6.30902e-5f;  // 1 GPM = 6.30902e-5 m³/s
static const float PSI_TO_PA  = 6894.76f;      // 1 PSI = 6894.76 Pa
String turbineSerialBuffer = "";
String turbineFilename = "";
float turbineP1 = 0.0;
float turbineP2 = 0.0;
float turbineFlowHz = 0.0;
float turbineGenFreqHz = 0.0;
float turbineVoltage = 0.0;
// Reserved for future DL24 serial integration; currently logged as blank values.
float turbineRLoad = -1.0;
float turbineCurrent = -1.0;
float turbinePower = -1.0;
float zeroP1 = 0.0;
float zeroP2 = 0.0;
float zeroSumP1 = 0.0;
float zeroSumP2 = 0.0;
int zeroSampleCount = 0;
bool turbineZeroReady = false;
unsigned long turbineLastDisplayMs = 0;
unsigned long turbineRowCount = 0;

// ---- Extended Turbine Bench (v2) ----

// Flow meter list (read from flowmeters.txt on SD)
#define MAX_FLOWMETERS 8
struct FlowMeterConfig {
  String name;
  float kHzPerGPM; // Q[GPM] = flow_hz / kHzPerGPM
};
FlowMeterConfig flowMeters[MAX_FLOWMETERS];
int numFlowMeters = 0;
int selectedFlowMeterIdx = 0;

// Turbine list (read from turbines.rxr on SD)
#define MAX_TURBINE_ENTRIES 8
struct TurbineEntry {
  char id;
  String name;
  String type;
};
TurbineEntry turbineEntries[MAX_TURBINE_ENTRIES];
int numTurbineEntries = 0;
int selectedTurbineEntryIdx = 0;
String turbineTestName = "";
char turbineTestId = '?';

// Pressure calibration curve (baseline dP with NO turbine installed)
#define NUM_CALIB_PTS 8
const float calibTargetGPM[NUM_CALIB_PTS] = {0.5f, 1.0f, 1.5f, 2.0f, 2.5f, 3.0f, 3.5f, 4.0f};
float calibDpPSI[NUM_CALIB_PTS] = {0};
bool calibValid = false;
int calibStep = 0;
float calibAccumDp = 0;
int calibAccumCount = 0;
unsigned long calibStepStart = 0;
const unsigned long CALIB_SAMPLE_MS = 10000; // 10 s per flow point

// Resistance sweep (user sets each value on DL24, DataBuddy reads V and logs)
const float SWEEP_R_OHMS[] = {1.0f, 2.0f, 5.0f, 10.0f, 20.0f, 50.0f, 100.0f,
                               200.0f, 500.0f, 1000.0f, 2000.0f};
const int NUM_SWEEP_R = 11;
int sweepRIdx = 0;
float sweepBestR = -1.0f;
float sweepBestPower = 0.0f;

// Test flow-rate sequence
const float TEST_FLOW_GPM[] = {0.5f, 1.0f, 1.5f, 2.0f, 2.5f, 3.0f, 3.5f, 4.0f};
const int NUM_TEST_FLOWS = 8;
int testFlowIdx = 0;
float turbineMinFlowGPM = 0.0f;

// Turbine test log file (separate from old turbine_NNNN files)
File turbTestFile;
String turbTestFilename = "";
unsigned long turbTestRows = 0;

// Shared encoder base for turbine sub-menu screens
long turbEncoderBase = 0;

// Reusable choice state for two-option menus (avoids static locals)
int turbMenuChoice = 0;


bool parseFloatField(const String& field, float& outValue) {
  char* endPtr = nullptr;
  outValue = strtof(field.c_str(), &endPtr);
  return endPtr != field.c_str() && *endPtr == '\0' && isfinite(outValue);
}

bool isAllDigits(const String& text) {
  int len = text.length();
  if (len == 0) return false;
  for (int i = 0; i < len; i++) {
    if (!isDigit(text.charAt(i))) return false;
  }
  return true;
}

bool parseTurbineLine(String line) {
  line.trim();
  if (line.length() == 0) return false;

  float values[5];
  int start = 0;
  for (int i = 0; i < 5; i++) {
    int commaIdx = line.indexOf(',', start);
    String token;
    if (i < 4) {
      if (commaIdx < 0) return false;
      token = line.substring(start, commaIdx);
      start = commaIdx + 1;
    } else {
      if (commaIdx >= 0) return false;
      token = line.substring(start);
    }
    token.trim();
    if (!parseFloatField(token, values[i])) return false;
  }

  turbineP1 = values[0];
  turbineP2 = values[1];
  turbineFlowHz = values[2];
  turbineGenFreqHz = values[3];
  turbineVoltage = values[4];
  return true;
}

bool readSerial1Line(String& outLine) {
  while (Serial1.available()) {
    char c = (char)Serial1.read();
    if (c == '\r') continue;
    if (c == '\n') {
      outLine = turbineSerialBuffer;
      turbineSerialBuffer = "";
      outLine.trim();
      if (outLine.length() > 0) return true;
      continue;
    }
    turbineSerialBuffer += c;
  }
  return false;
}

String getNextTurbineFilename() {
  int maxIndex = 0;
  File root = SD.open("/");
  if (!root) return "turbine_0001.csv";

  while (true) {
    File entry = root.openNextFile();
    if (!entry) break;
    if (!entry.isDirectory()) {
      String name = entry.name();
      String lower = name;
      lower.toLowerCase();
      if (lower.startsWith("turbine_") && lower.endsWith(".csv")) {
        String numPart = lower.substring(8, lower.length() - 4);
        if (numPart.length() == 4 && isAllDigits(numPart)) {
          int idx = numPart.toInt();
          if (idx > maxIndex) maxIndex = idx;
        }
      }
    }
    entry.close();
  }
  root.close();

  char filename[20];
  if (maxIndex >= 9999) {
    Serial.println("Turbine file index limit reached (9999). Delete old turbine_*.csv files.");
    return "";
  }
  int nextIndex = maxIndex + 1;
  snprintf(filename, sizeof(filename), "turbine_%04d.csv", nextIndex);
  return String(filename);
}

// ---- Extended Turbine Bench helpers (v2) ----

void readFlowMeters() {
  numFlowMeters = 0;
  File f = SD.open("flowmeters.txt", FILE_READ);
  if (!f) return;
  while (f.available() && numFlowMeters < MAX_FLOWMETERS) {
    String line = f.readStringUntil('\n');
    line.trim();
    if (line.length() == 0 || line.charAt(0) == '#') continue;
    int comma = line.indexOf(',');
    if (comma < 0) continue;
    float k = line.substring(comma + 1).toFloat();
    if (k <= 0) continue;
    flowMeters[numFlowMeters].name = line.substring(0, comma);
    flowMeters[numFlowMeters].kHzPerGPM = k;
    numFlowMeters++;
  }
  f.close();
}

void readTurbineList() {
  numTurbineEntries = 0;
  File f = SD.open("turbines.rxr", FILE_READ);
  if (f) {
    while (f.available() && numTurbineEntries < MAX_TURBINE_ENTRIES - 1) {  // -1 reserves slot for "Unlisted"
      String line = f.readStringUntil('\n');
      line.trim();
      if (line.length() == 0 || line.charAt(0) == '#') continue;
      int c1 = line.indexOf(',');
      int c2 = (c1 >= 0) ? line.indexOf(',', c1 + 1) : -1;
      if (c1 < 0) continue;
      turbineEntries[numTurbineEntries].id = line.charAt(0);
      turbineEntries[numTurbineEntries].name = line.substring(c1 + 1, c2 > 0 ? c2 : line.length());
      turbineEntries[numTurbineEntries].type = (c2 > 0) ? line.substring(c2 + 1) : "";
      numTurbineEntries++;
    }
    f.close();
  }
  // Always append "Unlisted" option
  turbineEntries[numTurbineEntries].id = '?';
  turbineEntries[numTurbineEntries].name = "Unlisted";
  turbineEntries[numTurbineEntries].type = "";
  numTurbineEntries++;
}

bool loadCalibration() {
  File f = SD.open("turb_cal.txt", FILE_READ);
  if (!f) return false;
  int count = 0;
  while (f.available() && count < NUM_CALIB_PTS) {
    String line = f.readStringUntil('\n');
    line.trim();
    if (line.length() == 0 || line.charAt(0) == '#') continue;
    int comma = line.indexOf(',');
    if (comma < 0) continue;
    calibDpPSI[count] = line.substring(comma + 1).toFloat();
    count++;
  }
  f.close();
  return (count == NUM_CALIB_PTS);
}

void saveCalibration() {
  SD.remove("turb_cal.txt");
  File f = SD.open("turb_cal.txt", FILE_WRITE);
  if (!f) return;
  f.println("# DataBuddy turbine bench baseline dP calibration");
  f.println("# flow_gpm,dp_psi");
  for (int i = 0; i < NUM_CALIB_PTS; i++) {
    f.print(calibTargetGPM[i], 2);
    f.print(",");
    f.println(calibDpPSI[i], 5);
  }
  f.close();
}

float interpolateCalibDp(float flowGPM) {
  if (flowGPM <= calibTargetGPM[0]) return calibDpPSI[0];
  if (flowGPM >= calibTargetGPM[NUM_CALIB_PTS - 1]) return calibDpPSI[NUM_CALIB_PTS - 1];
  for (int i = 0; i < NUM_CALIB_PTS - 1; i++) {
    if (flowGPM <= calibTargetGPM[i + 1]) {
      float t = (flowGPM - calibTargetGPM[i]) / (calibTargetGPM[i + 1] - calibTargetGPM[i]);
      return calibDpPSI[i] + t * (calibDpPSI[i + 1] - calibDpPSI[i]);
    }
  }
  return calibDpPSI[NUM_CALIB_PTS - 1];
}

float flowHzToGPM(float hz) {
  if (numFlowMeters == 0 || selectedFlowMeterIdx >= numFlowMeters) return 0.0f;
  float k = flowMeters[selectedFlowMeterIdx].kHzPerGPM;
  return (k > 0) ? hz / k : 0.0f;
}
String getNextTurbTestFilename() {
  int maxIndex = 0;
  File root = SD.open("/");
  if (!root) return "ttest_0001.csv";
  while (true) {
    File entry = root.openNextFile();
    if (!entry) break;
    if (!entry.isDirectory()) {
      String lower = entry.name();
      lower.toLowerCase();
      if (lower.startsWith("ttest_") && lower.endsWith(".csv")) {
        String numPart = lower.substring(6, lower.length() - 4);
        if (numPart.length() == 4 && isAllDigits(numPart)) {
          int idx = numPart.toInt();
          if (idx > maxIndex) maxIndex = idx;
        }
      }
    }
    entry.close();
  }
  root.close();
  if (maxIndex >= 9999) return "";
  char filename[20];
  snprintf(filename, sizeof(filename), "ttest_%04d.csv", maxIndex + 1);
  return String(filename);
}

// ---- Extended Turbine Bench state handlers (v2) ----

void turbineSelectFlowMeterScreen() {
  long newPos = knob.read();
  int delta = (newPos - turbEncoderBase) / menuEncoderDelay;
  if (delta != 0 && numFlowMeters > 0) {
    selectedFlowMeterIdx = (selectedFlowMeterIdx + delta + numFlowMeters) % numFlowMeters;
    turbEncoderBase += delta * menuEncoderDelay;
  }

  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.print("Select flow meter:");
  if (numFlowMeters > 0) {
    display.setTextSize(2);
    display.setCursor(0, 14);
    display.print(flowMeters[selectedFlowMeterIdx].name);
    display.setTextSize(1);
    display.setCursor(0, 46);
    display.print("K=");
    display.print(flowMeters[selectedFlowMeterIdx].kHzPerGPM, 1);
    display.print(" Hz/GPM");
  } else {
    display.setTextSize(1);
    display.setCursor(0, 20);
    display.print("No flowmeters.txt!");
    display.setCursor(0, 32);
    display.print("Add file to SD card.");
  }
  display.setTextSize(1);
  display.setCursor(0, 56);
  display.print("Press to select");
  drawBatteryIndicator();
  display.display();

  if (buttonPressed()) {
    if (numFlowMeters == 0) return;
    bool hasCalib = loadCalibration();
    calibValid = hasCalib;
    turbMenuChoice = 0;
    turbEncoderBase = knob.read();
    state = TURBINE_CALIB_CHOICE;
  }
}

void turbineCalibChoiceScreen() {
  if (!calibValid) {
    // No stored calibration — skip the choice and go straight to calibration
    display.clearDisplay();
    display.setTextColor(SSD1306_WHITE);
    printCentered("No stored cal", 0, 1);
    display.setTextSize(1);
    display.setCursor(0, 14);
    display.print("Remove turbine from");
    display.setCursor(0, 24);
    display.print("the line, then press");
    display.setCursor(0, 34);
    display.print("to begin calibration.");
    drawBatteryIndicator();
    display.display();
    if (buttonPressed()) {
      calibStep = 0;
      calibAccumDp = 0;
      calibAccumCount = 0;
      state = TURBINE_CALIB_SETFLOW;
    }
    return;
  }

  long newPos = knob.read();
  int delta = (newPos - turbEncoderBase) / menuEncoderDelay;
  if (delta != 0) {
    turbMenuChoice = (turbMenuChoice + delta + 2) % 2;
    turbEncoderBase += delta * menuEncoderDelay;
  }

  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  printCentered("Pressure Cal", 0, 1);
  display.setTextSize(1);
  display.setCursor(0, 14);
  display.print(turbMenuChoice == 0 ? "> Use stored cal" : "  Use stored cal");
  display.setCursor(0, 26);
  display.print(turbMenuChoice == 1 ? "> Recalibrate" : "  Recalibrate");
  display.setCursor(0, 50);
  display.print("Press to confirm");
  drawBatteryIndicator();
  display.display();

  if (buttonPressed()) {
    if (turbMenuChoice == 1) {
      calibStep = 0;
      calibAccumDp = 0;
      calibAccumCount = 0;
      state = TURBINE_CALIB_SETFLOW;
    } else {
      readTurbineList();
      selectedTurbineEntryIdx = 0;
      turbEncoderBase = knob.read();
      state = TURBINE_SELECT_TURBINE;
    }
    turbMenuChoice = 0;
  }
}

void turbineCalibSetFlowScreen() {
  String line;
  while (readSerial1Line(line)) parseTurbineLine(line);

  float currentFlow = flowHzToGPM(turbineFlowHz);
  float target = calibTargetGPM[calibStep];

  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  printCentered("CALIBRATE", 0, 2);
  display.setTextSize(1);
  display.setCursor(0, 18);
  display.print("No turbine in line!");
  display.setCursor(0, 28);
  display.print("Set flow to:");
  display.setTextSize(2);
  display.setCursor(0, 38);
  display.print(target, 1);
  display.print(" GPM");
  display.setTextSize(1);
  display.setCursor(0, 56);
  display.print("Now:");
  display.print(currentFlow, 2);
  display.print(" - Press OK");
  drawBatteryIndicator();
  display.display();

  if (buttonPressed()) {
    calibAccumDp = 0;
    calibAccumCount = 0;
    calibStepStart = millis();
    state = TURBINE_CALIB_ACQUIRE;
  }
}

void turbineCalibAcquireScreen() {
  String line;
  while (readSerial1Line(line)) {
    if (parseTurbineLine(line)) {
      calibAccumDp += (turbineP1 - turbineP2);
      calibAccumCount++;
    }
  }

  unsigned long elapsed = millis() - calibStepStart;
  float remaining = (float)(CALIB_SAMPLE_MS - min(elapsed, CALIB_SAMPLE_MS)) / 1000.0f;
  float currentDp = (calibAccumCount > 0) ? (calibAccumDp / calibAccumCount) : 0.0f;
  float currentFlow = flowHzToGPM(turbineFlowHz);

  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  printCentered("CAL RECORDING", 0, 1);
  display.setTextSize(1);
  display.setCursor(0, 12);
  display.print("Pt ");
  display.print(calibStep + 1);
  display.print("/");
  display.print(NUM_CALIB_PTS);
  display.print(" Tgt:");
  display.print(calibTargetGPM[calibStep], 1);
  display.print("GPM");
  display.setCursor(0, 24);
  display.print("ActQ: ");
  display.print(currentFlow, 2);
  display.print(" GPM");
  display.setCursor(0, 36);
  display.print("dP avg: ");
  display.print(currentDp, 4);
  display.print(" PSI");
  display.setCursor(0, 48);
  display.print("Time: ");
  display.print(remaining, 1);
  display.print("s");
  drawBatteryIndicator();
  display.display();

  if (elapsed >= CALIB_SAMPLE_MS) {
    calibDpPSI[calibStep] = (calibAccumCount > 0) ? (calibAccumDp / calibAccumCount) : 0.0f;
    calibStep++;
    if (calibStep >= NUM_CALIB_PTS) {
      turbMenuChoice = 0;
      turbEncoderBase = knob.read();
      state = TURBINE_CALIB_HIGHFLOW;
    } else {
      state = TURBINE_CALIB_SETFLOW;
    }
    calibAccumDp = 0;
    calibAccumCount = 0;
  }
}

void turbineCalibHighFlowScreen() {
  long newPos = knob.read();
  int delta = (newPos - turbEncoderBase) / menuEncoderDelay;
  if (delta != 0) {
    turbMenuChoice = (turbMenuChoice + delta + 2) % 2;
    turbEncoderBase += delta * menuEncoderDelay;
  }

  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  printCentered("Cal done!", 0, 1);
  display.setTextSize(1);
  display.setCursor(0, 12);
  display.print("8 points recorded.");
  display.setCursor(0, 24);
  display.print("Continue >4 GPM?");
  display.setCursor(0, 36);
  display.print(turbMenuChoice == 0 ? "> No (save & go)" : "  No (save & go)");
  display.setCursor(0, 48);
  display.print(turbMenuChoice == 1 ? "> Yes (TBD)" : "  Yes (TBD)");
  drawBatteryIndicator();
  display.display();

  if (buttonPressed()) {
    // High-flow extension (choice == 1) is reserved for a future firmware update.
    saveCalibration();
    calibValid = true;
    readTurbineList();
    selectedTurbineEntryIdx = 0;
    turbEncoderBase = knob.read();
    state = TURBINE_SELECT_TURBINE;
    turbMenuChoice = 0;
  }
}

void turbineSelectTurbineScreen() {
  long newPos = knob.read();
  int delta = (newPos - turbEncoderBase) / menuEncoderDelay;
  if (delta != 0 && numTurbineEntries > 0) {
    selectedTurbineEntryIdx = (selectedTurbineEntryIdx + delta + numTurbineEntries) % numTurbineEntries;
    turbEncoderBase += delta * menuEncoderDelay;
  }

  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.print("Select turbine:");
  if (numTurbineEntries > 0) {
    String tname = turbineEntries[selectedTurbineEntryIdx].name;
    if (tname.length() > 10) {
      display.setTextSize(1);
      display.setCursor(0, 14);
    } else {
      display.setTextSize(2);
      display.setCursor(0, 14);
    }
    display.print(tname);
    display.setTextSize(1);
    display.setCursor(0, 44);
    display.print("Type: ");
    display.print(turbineEntries[selectedTurbineEntryIdx].type);
  } else {
    display.setTextSize(1);
    display.setCursor(0, 20);
    display.print("No turbines.rxr!");
  }
  display.setTextSize(1);
  display.setCursor(0, 56);
  display.print("Press to select");
  drawBatteryIndicator();
  display.display();

  if (buttonPressed()) {
    if (numTurbineEntries == 0) return;
    turbineTestId = turbineEntries[selectedTurbineEntryIdx].id;
    turbineTestName = turbineEntries[selectedTurbineEntryIdx].name;
    if (turbineTestId == '?') {
      turbineTestId = 'A'; // default starting letter for unlisted
      turbEncoderBase = knob.read();
      state = TURBINE_UNLISTED_ID;
    } else {
      state = TURBINE_FIND_MIN_FLOW;
    }
  }
}

void turbineUnlistedIdScreen() {
  long newPos = knob.read();
  int delta = (newPos - turbEncoderBase) / menuEncoderDelay;
  if (delta != 0) {
    int letter = turbineTestId;
    letter = ((letter - 'A') + delta + 26) % 26 + 'A';
    turbineTestId = (char)letter;
    turbEncoderBase += delta * menuEncoderDelay;
  }

  char idStr[2] = {turbineTestId, '\0'};
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  printCentered("Unlisted Turbine", 0, 1);
  display.setTextSize(1);
  display.setCursor(0, 14);
  display.print("Assign a letter ID:");
  display.setTextSize(3);
  printCentered(idStr, 26, 3);
  display.setTextSize(1);
  display.setCursor(0, 56);
  display.print("Press to confirm");
  drawBatteryIndicator();
  display.display();

  if (buttonPressed()) {
    turbineTestName = String("Turbine-") + turbineTestId;
    state = TURBINE_FIND_MIN_FLOW;
  }
}

void turbineFindMinFlowScreen() {
  String line;
  while (readSerial1Line(line)) parseTurbineLine(line);

  float currentFlow = flowHzToGPM(turbineFlowHz);
  bool generating = (turbineVoltage > TURBINE_MIN_VOLTAGE_V);

  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  printCentered("FIND MIN FLOW", 0, 1);
  display.setCursor(0, 12);
  display.print("Install turbine!");
  display.setCursor(0, 22);
  display.print("Stop flow, then slowly");
  display.setCursor(0, 32);
  display.print("increase until stopped.");
  display.setCursor(0, 44);
  display.print("Q:");
  display.print(currentFlow, 2);
  display.print(" GPM  V:");
  display.print(turbineVoltage, 2);
  if (generating) {
    display.setTextSize(2);
    printCentered(">> STOP! <<", 52, 2);
    turbineMinFlowGPM = currentFlow;
  } else {
    display.setTextSize(1);
    display.setCursor(0, 56);
    display.print("Waiting for voltage...");
  }
  drawBatteryIndicator();
  display.display();

  // Press button once turbine is generating to mark min flow and start test
  if (generating && buttonPressed()) {
    turbineMinFlowGPM = currentFlow;
    turbTestFilename = getNextTurbTestFilename();
    if (turbTestFilename.length() == 0 || !sdAvailable) {
      state = SD_ERROR;
      return;
    }
    turbTestFile = SD.open(turbTestFilename.c_str(), FILE_WRITE);
    if (!turbTestFile) {
      state = SD_ERROR;
      return;
    }
    turbTestFile.print("# Turbine: ");
    turbTestFile.println(turbineTestName);
    turbTestFile.print("# ID: ");
    turbTestFile.println(turbineTestId);
    turbTestFile.print("# Flow meter: ");
    turbTestFile.println(numFlowMeters > 0 ? flowMeters[selectedFlowMeterIdx].name : "unknown");
    turbTestFile.print("# Min start flow GPM: ");
    turbTestFile.println(turbineMinFlowGPM, 3);
    // Columns: time, flow[GPM], R[ohm], V[V], I[A]=V/R, P_elec[W]=V^2/R,
    //          dP_raw[PSI], dP_baseline[PSI], dP_corrected[PSI],
    //          flow[m^3/s], dP[Pa], P_hydro[W]=Q*dP
    turbTestFile.println("timestamp_ms,flow_gpm,gen_freq_hz,R_ohm,V_gen_V,I_calc_A,P_elec_W,"
                         "dP_raw_PSI,dP_baseline_PSI,dP_corrected_PSI,"
                         "flow_m3s,dP_Pa,P_hydro_W");
    turbTestFile.flush();
    turbTestRows = 0;
    testFlowIdx = 0;
    sweepRIdx = 0;
    sweepBestR = -1.0f;
    sweepBestPower = 0.0f;
    state = TURBINE_SWEEPING;
  }
}

void turbineSweepingScreen() {
  String line;
  while (readSerial1Line(line)) parseTurbineLine(line);

  float currentFlow = flowHzToGPM(turbineFlowHz);
  float targetFlow = TEST_FLOW_GPM[testFlowIdx];
  float currentR = SWEEP_R_OHMS[sweepRIdx];
  float v = turbineVoltage;
  float iCalc = (currentR > 0) ? (v / currentR) : 0.0f;
  float pElec = v * iCalc; // = V²/R  [W]
  float dpRaw = turbineP1 - turbineP2;
  float dpBaseline = calibValid ? interpolateCalibDp(currentFlow) : 0.0f;
  float dpCorrected = dpRaw - dpBaseline;
  float flowM3s = currentFlow * GPM_TO_M3S; // GPM → m³/s
  float dpPa = dpCorrected * PSI_TO_PA;     // PSI → Pa
  float pHydro = flowM3s * dpPa;            // hydraulic power [W]

  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.print("SWP ");
  display.print(targetFlow, 1);
  display.print("G R");
  display.print(sweepRIdx + 1);
  display.print("/");
  display.print(NUM_SWEEP_R);
  display.setCursor(0, 10);
  display.print("Set CR=");
  display.print(currentR, 0);
  display.print("ohm");
  display.setCursor(0, 20);
  display.print("V:");
  display.print(v, 2);
  display.print(" I:");
  display.print(iCalc, 3);
  display.print("A");
  display.setCursor(0, 30);
  display.print("Pe:");
  display.print(pElec * 1000.0f, 0);
  display.print("mW Gf:");
  display.print(turbineGenFreqHz, 1);
  display.print("Hz");
  display.setCursor(0, 40);
  display.print("dP:");
  display.print(dpCorrected, 2);
  display.print("psi");
  display.setCursor(0, 50);
  display.print("Q:");
  display.print(currentFlow, 2);
  display.print("GPM  Press:log");
  drawBatteryIndicator();
  display.display();

  if (buttonPressed()) {
    // Log data point
    turbTestFile.print(millis());       turbTestFile.print(",");
    turbTestFile.print(currentFlow, 4); turbTestFile.print(",");
    turbTestFile.print(turbineGenFreqHz, 4); turbTestFile.print(",");
    turbTestFile.print(currentR, 1);    turbTestFile.print(",");
    turbTestFile.print(v, 4);           turbTestFile.print(",");
    turbTestFile.print(iCalc, 5);       turbTestFile.print(",");
    turbTestFile.print(pElec, 5);       turbTestFile.print(",");
    turbTestFile.print(dpRaw, 4);       turbTestFile.print(",");
    turbTestFile.print(dpBaseline, 4);  turbTestFile.print(",");
    turbTestFile.print(dpCorrected, 4); turbTestFile.print(",");
    turbTestFile.print(flowM3s, 8);     turbTestFile.print(",");
    turbTestFile.print(dpPa, 2);        turbTestFile.print(",");
    turbTestFile.println(pHydro, 4);
    turbTestRows++;
    turbTestFile.flush();

    if (pElec > sweepBestPower) {
      sweepBestPower = pElec;
      sweepBestR = currentR;
    }

    sweepRIdx++;
    if (sweepRIdx >= NUM_SWEEP_R) {
      // Finished sweep at this flow rate — advance to next flow
      sweepRIdx = 0;
      sweepBestR = -1.0f;
      sweepBestPower = 0.0f;
      testFlowIdx++;
      if (testFlowIdx >= NUM_TEST_FLOWS) {
        state = TURBINE_SAVING;
      } else {
        turbEncoderBase = knob.read();
        state = TURBINE_NEXT_FLOW;
      }
    }
  }
}

void turbineNextFlowScreen() {
  String line;
  while (readSerial1Line(line)) parseTurbineLine(line);

  float currentFlow = flowHzToGPM(turbineFlowHz);
  float target = TEST_FLOW_GPM[testFlowIdx];

  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  printCentered("NEXT FLOW", 0, 2);
  display.setTextSize(1);
  display.setCursor(0, 22);
  display.print("Set flow to:");
  display.setTextSize(2);
  display.setCursor(0, 34);
  display.print(target, 1);
  display.print(" GPM");
  display.setTextSize(1);
  display.setCursor(0, 56);
  display.print("Now:");
  display.print(currentFlow, 2);
  display.print("  Press OK");
  drawBatteryIndicator();
  display.display();

  if (buttonPressed()) {
    sweepRIdx = 0;
    state = TURBINE_SWEEPING;
  }
}

void turbineSavingScreen() {
  static unsigned long savingEnteredMs = 0;
  static bool fileClosed = false;

  if (savingEnteredMs == 0) {
    // First call: close file and record entry time
    if (turbTestFile) {
      turbTestFile.flush();
      turbTestFile.close();
    }
    fileClosed = true;
    savingEnteredMs = millis();
  }

  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  printCentered("Nice!", 0, 2);
  display.setTextSize(1);
  display.setCursor(0, 22);
  display.print("Saving to microSD...");
  display.setCursor(0, 34);
  display.print("File: ");
  display.print(turbTestFilename);
  display.setCursor(0, 46);
  display.print("Rows: ");
  display.print(turbTestRows);
  drawBatteryIndicator();
  display.display();

  if (millis() - savingEnteredMs >= 2500UL) {
    savingEnteredMs = 0;
    fileClosed = false;
    turbMenuChoice = 0;
    turbEncoderBase = knob.read();
    state = TURBINE_ASK_ANOTHER;
  }
}

void turbineAskAnotherScreen() {
  long newPos = knob.read();
  int delta = (newPos - turbEncoderBase) / menuEncoderDelay;
  if (delta != 0) {
    turbMenuChoice = (turbMenuChoice + delta + 2) % 2;
    turbEncoderBase += delta * menuEncoderDelay;
  }

  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  printCentered("Test another?", 0, 1);
  display.setTextSize(1);
  display.setCursor(0, 14);
  display.print("Test complete.");
  display.setCursor(0, 26);
  display.print(turbMenuChoice == 0 ? "> No (back to menu)" : "  No (back to menu)");
  display.setCursor(0, 38);
  display.print(turbMenuChoice == 1 ? "> Yes (same cal)" : "  Yes (same cal)");
  display.setCursor(0, 56);
  display.print("Press to confirm");
  drawBatteryIndicator();
  display.display();

  if (buttonPressed()) {
    if (turbMenuChoice == 1) {
      // Re-use existing calibration; pick a new turbine
      readTurbineList();
      selectedTurbineEntryIdx = 0;
      turbEncoderBase = knob.read();
      state = TURBINE_SELECT_TURBINE;
    } else {
      state = MENU;
      menuEncoderBase = knob.read();
    }
    turbMenuChoice = 0;
  }
}


void resetTurbineZeroing() {
  zeroSumP1 = 0.0;
  zeroSumP2 = 0.0;
  zeroSampleCount = 0;
  turbineZeroReady = false;
}

void turbineIdleScreen() {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  printCentered("TURBINE BENCH", 0, 2);
  display.setTextSize(1);
  printCentered("Pressure & power", 22, 1);
  printCentered("test system v2", 32, 1);
  printCentered("Press to begin", 48, 1);
  drawBatteryIndicator();
  display.display();

  if (buttonPressed()) {
    readFlowMeters();
    selectedFlowMeterIdx = 0;
    turbEncoderBase = knob.read();
    if (numFlowMeters > 0) {
      state = TURBINE_SELECT_FLOWMETER;
    } else {
      // No flowmeters.txt on SD — inform user and stay idle
      display.clearDisplay();
      display.setTextSize(1);
      printCentered("No flowmeters.txt!", 16, 1);
      printCentered("Add to SD root", 32, 1);
      drawBatteryIndicator();
      display.display();
      delay(2500);
    }
  }
}

void turbineZeroingScreen() {
  String line;
  while (readSerial1Line(line)) {
    if (parseTurbineLine(line)) {
      if (!turbineZeroReady && zeroSampleCount < TURBINE_ZERO_SAMPLES) {
        zeroSumP1 += turbineP1;
        zeroSumP2 += turbineP2;
        zeroSampleCount++;
        if (zeroSampleCount >= TURBINE_ZERO_SAMPLES) {
          zeroP1 = zeroSumP1 / TURBINE_ZERO_SAMPLES;
          zeroP2 = zeroSumP2 / TURBINE_ZERO_SAMPLES;
          turbineZeroReady = true;
        }
      }
    }
  }

  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  if (!turbineZeroReady) {
    printCentered("ZEROING...", 0, 2);
    printCentered(("P1: " + String(turbineP1, 2)).c_str(), 24, 2);
    printCentered(("P2: " + String(turbineP2, 2)).c_str(), 46, 2);
  } else {
    printCentered("Zero set!", 0, 2);
    display.setTextSize(1);
    display.setCursor(0, 24);
    display.print("P1_zero: ");
    display.print(zeroP1, 2);
    display.setCursor(0, 36);
    display.print("P2_zero: ");
    display.print(zeroP2, 2);
    display.setCursor(0, 50);
    display.print("Install turbine, press");
  }
  drawBatteryIndicator();
  display.display();

  if (turbineZeroReady && buttonPressed()) {
    if (!sdAvailable) {
      state = SD_ERROR;
      return;
    }
    turbineFilename = getNextTurbineFilename();
    if (turbineFilename.length() == 0) {
      state = SD_ERROR;
      return;
    }
    logFile = SD.open(turbineFilename.c_str(), FILE_WRITE);
    if (!logFile) {
      state = SD_ERROR;
      return;
    }
    logFile.println("timestamp_ms,P1_raw,P2_raw,dP,flow_hz,gen_freq_hz,voltage,R_load,current,power");
    logFile.flush();
    turbineRowCount = 0;
    turbineLastDisplayMs = 0;
    state = TURBINE_LOGGING;
  }
}

void turbineLoggingScreen() {
  String line;
  while (readSerial1Line(line)) {
    if (!parseTurbineLine(line)) continue;

    // Incoming pressure stream is expected in PSI.
    float dP = (turbineP1 - zeroP1) - (turbineP2 - zeroP2);
    logFile.print(millis());
    logFile.print(",");
    logFile.print(turbineP1, 3);
    logFile.print(",");
    logFile.print(turbineP2, 3);
    logFile.print(",");
    logFile.print(dP, 3);
    logFile.print(",");
    logFile.print(turbineFlowHz, 3);
    logFile.print(",");
    logFile.print(turbineGenFreqHz, 3);
    logFile.print(",");
    logFile.print(turbineVoltage, 3);
    logFile.print(",");
    if (turbineRLoad >= 0) logFile.print(turbineRLoad, 3);
    logFile.print(",");
    if (turbineCurrent >= 0) logFile.print(turbineCurrent, 3);
    logFile.print(",");
    if (turbinePower >= 0) logFile.print(turbinePower, 3);
    logFile.println();
    turbineRowCount++;
    if ((turbineRowCount % TURBINE_FLUSH_INTERVAL_ROWS) == 0) logFile.flush();
  }

  if (millis() - turbineLastDisplayMs >= TURBINE_DISPLAY_INTERVAL_MS) {
    turbineLastDisplayMs = millis();
    float dP = (turbineP1 - zeroP1) - (turbineP2 - zeroP2);
    display.clearDisplay();
    display.setTextColor(SSD1306_WHITE);
    display.setTextSize(1);
    display.setCursor(0, 0);
    display.print("dP: ");
    display.print(dP, 1);
    display.print("psi");
    display.setCursor(0, 12);
    display.print("Q:");
    display.print(turbineFlowHz, 1);
    display.print("Hz G:");
    display.print(turbineGenFreqHz, 0);
    display.print("Hz");
    display.setCursor(0, 24);
    display.print("V:");
    display.print(turbineVoltage, 1);
    display.print("V Pe:");
    if (turbinePower >= 0) {
      display.print(turbinePower, 0);
      display.print("mW");
    } else {
      display.print("--");
    }
    display.setCursor(0, 36);
    display.print("Rows: ");
    display.print(turbineRowCount);
    display.setCursor(0, 48);
    display.print("Press: stop");
    drawBatteryIndicator();
    display.display();
  }

  if (buttonPressed()) {
    if (logFile) {
      logFile.flush();
      logFile.close();
    }
    state = TURBINE_DONE;
  }
}

void turbineDoneScreen() {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  printCentered("TEST COMPLETE", 0, 2);
  display.setTextSize(1);
  display.setCursor(0, 24);
  display.print("Rows: ");
  display.print(turbineRowCount);
  display.setCursor(0, 36);
  display.print("File: ");
  display.print(turbineFilename);
  display.setCursor(0, 52);
  display.print("Press: menu");
  drawBatteryIndicator();
  display.display();

  if (buttonPressed()) {
    state = MENU;
    menuEncoderBase = knob.read();
  }
}

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
  if (!usingBattery) return;
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
    } else if (menuIndex == 3) {
      state = TURBINE_IDLE;
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
  display.setTextSize(1);
  printCentered("Press to begin!", 56, 1);
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
  if (usingBattery) {
    for(int i=0;i<BATTERY_SAMPLES;i++) updateBatteryBuffer();
  }
  for(int i=0;i<10;i++) lastManualValues[i] = 0.0;
  encoderPos = knob.read();
  lastEncoderPos = encoderPos;
  menuEncoderBase = encoderPos;
}

void loop() {
  if (usingBattery) updateBatteryBuffer();
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
    case TURBINE_IDLE: turbineIdleScreen(); break;
    case TURBINE_ZEROING: turbineZeroingScreen(); break;
    case TURBINE_LOGGING: turbineLoggingScreen(); break;
    case TURBINE_DONE: turbineDoneScreen(); break;
    case TURBINE_SELECT_FLOWMETER: turbineSelectFlowMeterScreen(); break;
    case TURBINE_CALIB_CHOICE: turbineCalibChoiceScreen(); break;
    case TURBINE_CALIB_SETFLOW: turbineCalibSetFlowScreen(); break;
    case TURBINE_CALIB_ACQUIRE: turbineCalibAcquireScreen(); break;
    case TURBINE_CALIB_HIGHFLOW: turbineCalibHighFlowScreen(); break;
    case TURBINE_SELECT_TURBINE: turbineSelectTurbineScreen(); break;
    case TURBINE_UNLISTED_ID: turbineUnlistedIdScreen(); break;
    case TURBINE_FIND_MIN_FLOW: turbineFindMinFlowScreen(); break;
    case TURBINE_SWEEPING: turbineSweepingScreen(); break;
    case TURBINE_NEXT_FLOW: turbineNextFlowScreen(); break;
    case TURBINE_SAVING: turbineSavingScreen(); break;
    case TURBINE_ASK_ANOTHER: turbineAskAnotherScreen(); break;
  }
}
