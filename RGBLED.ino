// MKR WiFi 1010 NINA-module status LED, independent of Vespera RGB.
void LedRed() {
  WiFiDrv::analogWrite(25, 155);
  WiFiDrv::analogWrite(26, 0);
  WiFiDrv::analogWrite(27, 0);
}
void LedGreen() {
  WiFiDrv::analogWrite(25, 0);
  WiFiDrv::analogWrite(26, 155);
  WiFiDrv::analogWrite(27, 0);
}
void LedBlue() {
  WiFiDrv::analogWrite(25, 0);
  WiFiDrv::analogWrite(26, 0);
  WiFiDrv::analogWrite(27, 155);
}
