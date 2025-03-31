# Challenges I faced:

## USB Serial Disconnection Issue on Linux (CH340 + brltty)

### Problem

When connecting the ESP32 to a Linux system (e.g., Ubuntu), the device appeared briefly as `/dev/ttyUSB0`, then vanished immediately. Running `ls /dev/ttyUSB*` would show `/dev/ttyUSB0` for a split second, and then nothing.

### Symptoms

- `ls /dev/ttyUSB*` shows the device only for a moment
- `idf.py flash` results in: A fatal error occurred: Failed to connect to ESP32: No serial data received.

### Cause

The process **`brltty`** (Braille display support for Linux) was claiming the CH340 USB-to-Serial adapter used by the ESP32, causing it to disconnect.

### Solution

#### Remove `brltty`

If you do not need Braille support on your machine, you can safely uninstall it:

```bash
sudo apt remove brltty
```

Then reboot or unplug/replug the ESP32. The device should now remain attached as /dev/ttyUSB0.
