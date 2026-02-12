# bar
Firmware for the Firebeetle ESP32-E embedded in the sensor bar, part of a larger AR bartender game hardware architecture.

## Hardware Components

### Sensors

**INMP441 MEMS Microphone (I2S)**
- Digital I2S microphone for capturing voice audio
- Samples at 16kHz, 32-bit, mono (left channel)
- Connected via I2S bus (SCK=14, WS=17, SD=27)
- Reads in batches of 512 samples during recording

**Push Button**
- Momentary push button on pin 26 with internal pull-up
- Polled at ~30Hz (33ms interval) for falling-edge detection
- Acts as the recording trigger (push-to-talk toggle)

**Hall Effect Sensors x5**
- Five analog hall effect sensors on pins A0-A4
- Detect magnetic force/proximity
- Calibrated on startup by averaging 30 baseline readings per sensor
- Polled at ~30Hz (33ms interval)
- Report offset values (current reading minus baseline)

### Actuation

**Recording LED**
- Indicates when mic recording/streaming is active

**Hall LEDs x5**
- One LED per hall sensor
- The LED corresponding to the closest (strongest) hall sensor lights up; all others are off

### Communication

**MQTT over TLS**
- WiFi + TLS MQTT connection to EMQX Cloud broker (port 8883)
- Two topics:
  - `glove/audio` — binary audio fragments streamed during recording
  - `glove/hall` — JSON with the index of the strongest hall sensor
- Uses PubSubClient with WiFiClientSecure

## Architecture

### Dual-Core Design (FreeRTOS)

**Core 1** runs `setup()` and `loop()` — polls sensors and drives the state machine.
**Core 0** runs two MQTT publish tasks that drain FreeRTOS queues.

The cores communicate via queues (`audioQueue`, `hallQueue`) and share the MQTT client behind a mutex.

### Core 1: Main Loop

```
+---------------------------------------------------+
|                    Main Loop                       |
|                                                    |
|  1. Poll button (~30Hz)                            |
|     +- press detected? -> advance state machine    |
|                                                    |
|  2. State machine (see below)                      |
|                                                    |
|  3. Poll hall sensors (~30Hz)                      |
|     +- Find closest (strongest) sensor             |
|     +- Light its LED, turn off the rest            |
|     +- Overwrite hallQueue with closest index      |
+---------------------------------------------------+
```

### Core 0: MQTT Tasks

```
+----------------------------------------------+
| hallPublishTask (priority 2)                 |
|   +- Take mutex, call mqtt.loop()           |
|   +- If hallQueue has data, publish JSON     |
|   +- Release mutex, sleep 2s                |
+----------------------------------------------+
| audioPublishTask (priority 1)                |
|   +- Block on audioQueue                     |
|   +- Take mutex, drain + publish all frags   |
|   +- Release mutex                           |
+----------------------------------------------+
```

### State Machine

```
        button press              button press OR 3s elapsed
IDLE --------------------> RECORDING ---------------------------> END --> IDLE
                           (mic read + enqueue fragments)    (enqueue sentinel,
                                                              LED off)
```

- **IDLE**: LED off. Waits for button press, then assigns a new `messageId` and turns LED on.
- **RECORDING**: Reads mic samples, splits them into 1024-byte chunks, prepends a 4-byte header (`messageId` + `fragmentId`, little-endian), and enqueues each fragment.
- **END**: Enqueues a sentinel fragment (`fragId = 0xFFFF`, no audio payload) to signal end-of-session, turns LED off, returns to IDLE.
