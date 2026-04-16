#include <Wire.h>
#include <Adafruit_ADS1X15.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

/*
  Sensor MCU firmware for Arduino Nano ESP32
  ------------------------------------------
  Reads:
    - ADS1115 A0: Pressure sensor P1 voltage (Grundfos RPS white wire)
    - ADS1115 A1: Pressure sensor P2 voltage (Grundfos RPS white wire)
    - ADS1115 A2: Rectified generator DC bus sense voltage (divider output, optional)
    - D2 interrupt: Allegro A1220 flow pulse input (10k pull-up required)
    - D3 interrupt: PC817 optocoupler pulse input for generator frequency

  Outputs over UART (Serial1 TX) at ~10 Hz:
    P1_voltage,P2_voltage,flow_hz,gen_freq_hz,dc_voltage
*/

static const uint8_t FLOW_PIN = 2;      // Nano ESP32 D2
static const uint8_t GEN_FREQ_PIN = 3;  // Nano ESP32 D3
static const uint8_t MODE_BUTTON_PIN = 4; // Nano ESP32 D4, momentary to GND
static const uint32_t UART_BAUD = 115200;
static const uint32_t SAMPLE_INTERVAL_MS = 100; // ~10 Hz
static const uint32_t DISPLAY_INTERVAL_MS = 200; // 5 Hz
static const uint32_t DEBOUNCE_DELAY_MS = 40;

Adafruit_ADS1115 ads;
Adafruit_SSD1306 display(128, 64, &Wire, -1);

volatile uint32_t flowPulseCount = 0;
volatile uint32_t genPulseCount = 0;

uint32_t lastSampleMs = 0;
uint32_t lastFlowCount = 0;
uint32_t lastGenCount = 0;
uint32_t lastDisplayMs = 0;

bool adsReady = false;
bool displayReady = false;
float p1Voltage = 0.0f;
float p2Voltage = 0.0f;
float dcVoltage = 0.0f;
float flowHz = 0.0f;
float genFreqHz = 0.0f;

enum DisplayPage {
  PAGE_SYSTEM_CHECK = 0,
  PAGE_DELTA_P,
  PAGE_FLOW,
  PAGE_FREQUENCY,
  PAGE_VOLTAGE,
  PAGE_RESISTANCE,
  PAGE_COUNT
};
DisplayPage currentPage = PAGE_SYSTEM_CHECK;

bool lastButtonState = HIGH;
uint32_t lastButtonEdgeMs = 0;

void IRAM_ATTR onFlowPulse() {
  flowPulseCount++;
}

void IRAM_ATTR onGenPulse() {
  genPulseCount++;
}

void handleModeButton() {
  bool currentState = digitalRead(MODE_BUTTON_PIN);
  if (currentState != lastButtonState && (millis() - lastButtonEdgeMs) > DEBOUNCE_DELAY_MS) {
    lastButtonEdgeMs = millis();
    lastButtonState = currentState;
    if (currentState == LOW) {
      currentPage = (DisplayPage)((currentPage + 1) % PAGE_COUNT);
    }
  }
}

void drawStatusDisplay() {
  if (!displayReady) return;

  float dP = p1Voltage - p2Voltage;
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);

  switch (currentPage) {
    case PAGE_SYSTEM_CHECK:
      display.setCursor(0, 0);
      display.print("SYSTEM CHECK");
      display.setCursor(0, 14);
      display.print("ADS1115: ");
      display.print(adsReady ? "OK" : "ERR");
      display.setCursor(0, 26);
      display.print("Flow Hz: ");
      display.print(flowHz, 1);
      display.setCursor(0, 38);
      display.print("Gen Hz:  ");
      display.print(genFreqHz, 1);
      display.setCursor(0, 50);
      display.print("UART CSV @ ~10Hz");
      break;
    case PAGE_DELTA_P:
      display.setCursor(0, 0);
      display.print("DELTA P");
      display.setCursor(0, 16);
      display.print("P1: ");
      display.print(p1Voltage, 3);
      display.print("V");
      display.setCursor(0, 28);
      display.print("P2: ");
      display.print(p2Voltage, 3);
      display.print("V");
      display.setCursor(0, 44);
      display.print("dP: ");
      display.print(dP, 3);
      display.print("V");
      break;
    case PAGE_FLOW:
      display.setCursor(0, 0);
      display.print("FLOW RATE");
      display.setCursor(0, 20);
      display.print("Flow: ");
      display.print(flowHz, 2);
      display.print(" Hz");
      display.setCursor(0, 48);
      display.print("Btn: next page");
      break;
    case PAGE_FREQUENCY:
      display.setCursor(0, 0);
      display.print("FREQUENCY");
      display.setCursor(0, 20);
      display.print("Gen: ");
      display.print(genFreqHz, 2);
      display.print(" Hz");
      display.setCursor(0, 48);
      display.print("Btn: next page");
      break;
    case PAGE_VOLTAGE:
      display.setCursor(0, 0);
      display.print("VOLTAGE");
      display.setCursor(0, 14);
      display.print("P1: ");
      display.print(p1Voltage, 3);
      display.print(" V");
      display.setCursor(0, 26);
      display.print("P2: ");
      display.print(p2Voltage, 3);
      display.print(" V");
      display.setCursor(0, 38);
      display.print("DC: ");
      display.print(dcVoltage, 3);
      display.print(" V");
      break;
    case PAGE_RESISTANCE:
      display.setCursor(0, 0);
      display.print("RESISTANCE");
      display.setCursor(0, 18);
      display.print("R load is set on");
      display.setCursor(0, 30);
      display.print("DL24 (not sensed");
      display.setCursor(0, 42);
      display.print("by this Nano MCU)");
      break;
    default:
      break;
  }

  display.display();
}

void setup() {
  pinMode(FLOW_PIN, INPUT_PULLUP);
  pinMode(GEN_FREQ_PIN, INPUT_PULLUP);
  pinMode(MODE_BUTTON_PIN, INPUT_PULLUP);

  Wire.begin();             // Nano ESP32 default SDA/SCL pins
  Serial.begin(115200);     // USB serial for debug
  Serial1.begin(UART_BAUD); // UART TX to Teensy Serial1 RX

  adsReady = ads.begin(0x48);
  if (!adsReady) {
    Serial.println("ERROR: ADS1115 not found at 0x48");
  } else {
  // ±6.144V range; ADS1115 LSB size is 0.1875mV at this gain.
  ads.setGain(GAIN_TWOTHIRDS);
  }

  displayReady = display.begin(SSD1306_SWITCHCAPVCC, 0x3C);
  if (!displayReady) {
    Serial.println("WARN: Display not detected at 0x3C");
  } else {
    display.clearDisplay();
    display.display();
  }

  attachInterrupt(digitalPinToInterrupt(FLOW_PIN), onFlowPulse, RISING);
  attachInterrupt(digitalPinToInterrupt(GEN_FREQ_PIN), onGenPulse, RISING);

  lastSampleMs = millis();
  lastDisplayMs = millis();
}

void loop() {
  handleModeButton();

  uint32_t now = millis();
  uint32_t elapsedMs = now - lastSampleMs;
  if (elapsedMs < SAMPLE_INTERVAL_MS) {
    if ((now - lastDisplayMs) >= DISPLAY_INTERVAL_MS) {
      drawStatusDisplay();
      lastDisplayMs = now;
    }
    return;
  }

  if (adsReady) {
    int16_t rawP1 = ads.readADC_SingleEnded(0);
    int16_t rawP2 = ads.readADC_SingleEnded(1);
    int16_t rawDc = ads.readADC_SingleEnded(2);
    p1Voltage = ads.computeVolts(rawP1);
    p2Voltage = ads.computeVolts(rawP2);
    dcVoltage = ads.computeVolts(rawDc);
  } else {
    p1Voltage = 0.0f;
    p2Voltage = 0.0f;
    dcVoltage = 0.0f;
  }

  uint32_t flowCountSnapshot;
  uint32_t genCountSnapshot;
  noInterrupts();
  flowCountSnapshot = flowPulseCount;
  genCountSnapshot = genPulseCount;
  interrupts();

  float dt = ((float)elapsedMs) / 1000.0f;
  if (dt <= 0.0f) {
    lastSampleMs = now;
    return;
  }
  flowHz = (flowCountSnapshot - lastFlowCount) / dt;
  genFreqHz = (genCountSnapshot - lastGenCount) / dt;

  lastFlowCount = flowCountSnapshot;
  lastGenCount = genCountSnapshot;
  lastSampleMs = now;

  Serial1.print(p1Voltage, 4);
  Serial1.print(",");
  Serial1.print(p2Voltage, 4);
  Serial1.print(",");
  Serial1.print(flowHz, 3);
  Serial1.print(",");
  Serial1.print(genFreqHz, 3);
  Serial1.print(",");
  Serial1.println(dcVoltage, 4);

  if ((now - lastDisplayMs) >= DISPLAY_INTERVAL_MS) {
    drawStatusDisplay();
    lastDisplayMs = now;
  }
}
