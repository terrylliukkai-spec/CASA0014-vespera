A gesture-controlled Vespera light using an ultrasonic distance sensor, with LIVE and LOCK modes.
Users can select a colour by moving their hand, then lock it so the colour remains when they move away.
Data Flow:
Hand position
    ↓
HC-SR04: echo duration
    ↓
Arduino: distance → gesture logic → RGB values
    ↓
Wi-Fi connection → MQTT broker
    ↓
Vespera: LED colour
