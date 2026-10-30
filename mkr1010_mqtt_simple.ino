// Vespera gesture controller (no screen or external button), based on Duncan Wilson's Sept 2026 workshop sketch.
// Board: Arduino MKR WiFi 1010. Keep all three .ino files in this folder.
#include <SPI.h>
#include <WiFiNINA.h>
#include <PubSubClient.h>
#include <utility/wifi_drv.h>
#include <math.h>
#include "arduino_secrets.h"

#ifndef SECRET_SSID1
#define SECRET_SSID1 ""
#endif
#ifndef SECRET_PASS1
#define SECRET_PASS1 ""
#endif

const char* ssid = SECRET_SSID;
const char* password = SECRET_PASS;
const char* ssid1 = SECRET_SSID1;
const char* password1 = SECRET_PASS1;
const char* mqtt_username = SECRET_MQTTUSER;
const char* mqtt_password = SECRET_MQTTPASS;
const char* mqtt_server = "mqtt.cetools.org";
const int mqtt_port = 1884;
String lightId = "18";
String mqtt_topic = "student/CASA0014/luminaire/" + lightId;
String clientId;
WiFiClient wifiClient;
PubSubClient mqttClient(wifiClient);
const int num_leds = 72;
const int payload_size = num_leds * 3;
byte RGBpayload[payload_size];

const uint8_t TRIG_PIN = 9, ECHO_PIN = 10;
const float MIN_CM = 5.0f, MAX_CM = 30.0f;
const float WHITE_DISTANCE_CM = MAX_CM;
const uint32_t WHITE_CONFIRM_MS = 300;
bool absenceActive = false;
uint32_t absenceSince = 0;
const uint32_t LEAVE_REARM_MS = 600;
bool leaveActive = false;
uint32_t leaveSince = 0;
uint8_t missedSamples = 0;
const float HOVER_TOLERANCE_CM = 2.0f, REARM_CM = 1.0f;
const uint32_t HOVER_MS = 2000, SAMPLE_MS = 80, MAX_SAMPLE_GAP_MS = 250;
bool wifiOK = false, mqttOK = false;
bool locked = false, pendingPublish = true;
bool distanceValid = false, filterReady = false;
bool hoverActive = false, gestureArmed = true;
float distanceCm = 0, hoverAnchor = 0, toggleAnchor = 0;
uint8_t rearmSamples = 0;
uint8_t red = 255, green = 0, blue = 0;
uint8_t lastRed = 0, lastGreen = 0, lastBlue = 0;
bool havePublished = false;
uint32_t hoverSince = 0, lastSample = 0, lastPublish = 0;

void setup() {
  Serial.begin(115200); // Do not wait for a USB serial terminal.
  pinMode(TRIG_PIN, OUTPUT);
  digitalWrite(TRIG_PIN, LOW);
  pinMode(ECHO_PIN, INPUT); // NEVER connect 5 V Echo directly.
  WiFiDrv::pinMode(25, OUTPUT);
  WiFiDrv::pinMode(26, OUTPUT);
  WiFiDrv::pinMode(27, OUTPUT);
  LedRed();
  byte mac[6];
  WiFi.macAddress(mac);
  char macStr[13];
  snprintf(macStr, sizeof(macStr), "%02X%02X%02X%02X%02X%02X",
           mac[5], mac[4], mac[3], mac[2], mac[1], mac[0]);
  clientId = "MKR1010_Sender_" + String(macStr);
  WiFi.setTimeout(1500);
  wifiClient.setConnectionTimeout(1000);
  mqttClient.setServer(mqtt_server, mqtt_port);
  mqttClient.setBufferSize(512);
  mqttClient.setKeepAlive(30);
  mqttClient.setSocketTimeout(1);
  Serial.println(F("Vespera gesture controller ready"));
  Serial.println(mqtt_topic);
}

void loop() {
  updateDistance(millis());
  serviceConnections(millis());
  // Library network calls are synchronous. Never count a connection stall
  // as evidence that the hand stayed stationary.
  updateDistance(millis());
  publishState(millis());
}

void updateDistance(uint32_t now) {
  if (now - lastSample < SAMPLE_MS) return;
  uint32_t sampleElapsed = now - lastSample;
  if (now - lastSample > MAX_SAMPLE_GAP_MS) {
    hoverActive = false;
    rearmSamples = 0;
    filterReady = false;
    absenceActive = false;
    leaveActive = false;
    missedSamples = 0;
  }
  lastSample = now;
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);
  // Bounded blocking read: at most 15 ms per sample (roughly 2.6 m).
  unsigned long duration = pulseIn(ECHO_PIN, HIGH, 15000UL);
  float raw = duration * 0.0343f / 2.0f;
  // A missing echo can mean the hand has left the sensor's field of view.
  // Confirm across consecutive samples; never replace a LOCK color.
  bool handAbsent = duration == 0 || raw > WHITE_DISTANCE_CM;
  // Sustained departure also rearms: the hand can return to the same position.
  // A single missing echo is not enough; LOCK color is never changed here.
  if (!gestureArmed && handAbsent) {
    if (!leaveActive) {
      leaveActive = true;
      leaveSince = now;
    }
    if (now - leaveSince >= LEAVE_REARM_MS) {
      gestureArmed = true;
      hoverActive = false;
      rearmSamples = 0;
      leaveActive = false;
      Serial.println(F("Gesture ready: return and hold"));
    }
  } else leaveActive = false;
  if (!locked && handAbsent) {
    if (!absenceActive) {
      absenceActive = true;
      absenceSince = now;
    }
    if (now - absenceSince >= WHITE_CONFIRM_MS &&
        (red != 255 || green != 255 || blue != 255)) {
      red = green = blue = 255;
      pendingPublish = true;
    }
  } else absenceActive = false;
  distanceValid = duration != 0 && raw >= 2.0f && raw <= 200.0f;
  if (!distanceValid) {
    filterReady = false;
    rearmSamples = 0;
    if (missedSamples < 3) ++missedSamples;
    // Pause, rather than count, up to two short missed samples.
    if (hoverActive) {
      if (duration == 0 && missedSamples <= 2)
        hoverSince += sampleElapsed;
      else hoverActive = false;
    }
    return;
  }
  missedSamples = 0;
  distanceCm = filterReady ? distanceCm + 0.35f * (raw - distanceCm) : raw;
  filterReady = true;
  bool inRange = raw >= MIN_CM && raw <= MAX_CM;
  // Restart smoothing on re-entry instead of carrying a far-away background.
  if (!inRange) filterReady = false;
  if (!locked && inRange) colorFromDistance(distanceCm);
  // Use unfiltered readings for gestures so smoothing cannot hide movement.
  if (!gestureArmed) {
    hoverActive = false;
    if (fabsf(raw - toggleAnchor) >= REARM_CM) {
      if (++rearmSamples >= 3) {
        gestureArmed = true;
        rearmSamples = 0;
        leaveActive = false;
        Serial.println(F("Gesture ready: hold still"));
      }
    } else rearmSamples = 0;
    return;
  }
  if (!inRange) {
    hoverActive = false;
    return;
  }
  if (!hoverActive || fabsf(raw - hoverAnchor) > HOVER_TOLERANCE_CM) {
    hoverActive = true;
    hoverAnchor = raw; // Fixed anchor: slow drift cannot accumulate forever.
    hoverSince = now;
    return;
  }
  if (now - hoverSince >= HOVER_MS) {
    locked = !locked;
    Serial.println(locked ? F("Mode: LOCK") : F("Mode: LIVE"));
    if (!locked) colorFromDistance(distanceCm);
    pendingPublish = true;
    gestureArmed = false;
    toggleAnchor = raw;
    rearmSamples = 0;
    hoverActive = false;
    leaveActive = false;
    missedSamples = 0;
  }
}

void colorFromDistance(float cm) {
  cm = constrain(cm, MIN_CM, MAX_CM);
  // Full hue sweep: 5 cm red, 17.5 cm green, 30 cm blue.
  float hue = 240.0f * (cm - MIN_CM) / (MAX_CM - MIN_CM);
  float segment = hue / 60.0f;
  int sector = (int)segment;
  uint8_t up = (uint8_t)((segment - sector) * 255 + 0.5f);
  uint8_t down = 255 - up;
  switch (sector) {
    case 0: red = 255; green = up; blue = 0; break;
    case 1: red = down; green = 255; blue = 0; break;
    case 2: red = 0; green = 255; blue = up; break;
    case 3: red = 0; green = down; blue = 255; break;
    default: red = 0; green = 0; blue = 255; break;
  }
}




