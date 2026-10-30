// Retry scheduling replaces the original blocking retry/delay loops.
// WiFiNINA/PubSubClient calls themselves can still block briefly.
void serviceConnections(uint32_t now) {
  static uint32_t wifiAttempt = 0, mqttAttempt = 0;
  static bool firstWifi = true, firstMQTT = true, alternate = false;
  wifiOK = WiFi.status() == WL_CONNECTED;
  if (!wifiOK) {
    mqttOK = false;
    pendingPublish = true;
    if (firstWifi || now - wifiAttempt >= 10000) {
      firstWifi = false;
      wifiClient.stop();
      LedBlue();
      if (WiFi.status() != WL_NO_MODULE) {
        bool useSecondary = alternate && ssid1[0] != '\0';
        WiFi.begin(useSecondary ? ssid1 : ssid, useSecondary ? password1 : password);
        alternate = !alternate;
      }
      wifiAttempt = millis();
      hoverActive = false;
      rearmSamples = 0;
    }
    return;
  }
  if (mqttClient.connected()) mqttClient.loop();
  mqttOK = mqttClient.connected();
  if (!mqttOK) {
    pendingPublish = true;
    if (firstMQTT || now - mqttAttempt >= 5000) {
      firstMQTT = false;
      wifiClient.stop();
      LedBlue();
      mqttOK = mqttClient.connect(clientId.c_str(), mqtt_username, mqtt_password);
      mqttAttempt = millis();
      hoverActive = false;
      rearmSamples = 0;
      if (mqttOK) LedGreen();
      else LedRed();
      // Pure publisher: preserve original protocol, no subscription or LWT.
    }
  }
}

bool send_colour_to_all(int r, int g, int b) {
  if (!mqttClient.connected()) return false;
  for (int pixel = 0; pixel < num_leds; ++pixel) {
    RGBpayload[pixel * 3] = (byte)constrain(r, 0, 255);
    RGBpayload[pixel * 3 + 1] = (byte)constrain(g, 0, 255);
    RGBpayload[pixel * 3 + 2] = (byte)constrain(b, 0, 255);
  }
  return mqttClient.publish(mqtt_topic.c_str(), RGBpayload, payload_size, false);
}


void publishState(uint32_t now) {
  if (!mqttClient.connected() || now - lastPublish < 100) return;
  uint8_t r = red, g = green, b = blue;
  bool changed = !havePublished || r != lastRed || g != lastGreen || b != lastBlue;
  if (!pendingPublish && !changed && now - lastPublish < 5000) return;
  // QoS 0 has no lamp acknowledgement; retry/refresh latest target periodically.
  lastPublish = now;
  if (send_colour_to_all(r, g, b)) {
    lastRed = r; lastGreen = g; lastBlue = b;
    havePublished = true;
    pendingPublish = false;
  } else pendingPublish = true;
}

