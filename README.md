# bar
Firmware for the Firebeetle ESP32-E embedded in the sensor bar, part of a larger AR bartender game hardware architecture.

## Hardware Components

### Sensors

**INMP441 MEMS Microphone (I2S)**
- Digital I2S microphone for capturing voice audio
- Samples at 16kHz, 32-bit, mono (left channel)
- Connected via I2S bus (SCK=14, WS=17, SD=27)
- Reads in batches of 64 samples every 20ms

**Push Button**
- Momentary push button on pin 26 with internal pull-up
- Polled at ~30Hz for falling-edge detection
- Acts as the recording trigger (push-to-talk toggle)

**Hall Effect Sensors x5**
- Five analog hall effect sensors on pins A0-A4
- Detect magnetic force/proximity
- Calibrated on startup by averaging 30 baseline readings per sensor
- Report offset values (current reading minus baseline)

### Actuation

**Recording LED**
- Indicates when mic recording/streaming is active

**Hall LEDs x5**
- One LED per hall sensor
- Lights up when its corresponding sensor detects a magnetic force above a threshold

### Communication

**MQTT over TLS**
- WiFi + TLS MQTT connection to EMQX Cloud broker (port 8883)
- Streams mic audio data to a remote server during recording
- Uses PubSubClient with ArduinoJson for structured messages

## Architecture

### Main Loop

```
+---------------------------------------------+
|                  Main Loop                   |
|                                              |
|  1. Poll button (30Hz)                       |
|     +- press detected? -> toggle record state|
|                                              |
|  2. Check record state                       |
|     +- IF recording:                         |
|     |   +- Read mic samples (20ms batches)   |
|     |   +- Stream samples over MQTT          |
|     |   +- Check elapsed time >= 3s?         |
|     |   |   +- YES -> stop recording         |
|     |   +- Set recording LED HIGH            |
|     +- ELSE:                                 |
|     |   +- Set recording LED LOW             |
|     |                                        |
|  3. Poll hall sensors (1Hz)                  |
|     +- For each of 5 sensors:                |
|         +- |offset| > threshold? -> LED HIGH |
|         +- otherwise             -> LED LOW  |
|                                              |
|  4. mqttClient.loop() (keepalive/reconnect)  |
+---------------------------------------------+
```

### State Machine (Recording)

```
        button press              button press OR 3s elapsed
IDLE --------------------> RECORDING ---------------------------> IDLE
                           (mic read + MQTT stream + LED on)
```

### Sensor-Algorithm-Actuation Loop

- **Sensor**: Raw readings from button, mic, and hall effect sensors
- **Algorithm**: State management (recording toggle, 3s timeout, hall threshold comparison)
- **Actuation**: LEDs reflect current state, MQTT streams voice data to the remote server
