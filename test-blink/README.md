# Pico WH LED Blink Test

A minimal hardware verification test for the Raspberry Pi Pico WH.

## Purpose

This test firmware verifies that your Pico WH hardware is functioning correctly before attempting to run the full K3s node firmware. It tests:

- USB serial communication
- WiFi chip initialization (required for LED control)
- Onboard LED control
- Basic firmware execution

## Requirements

- Raspberry Pi Pico WH (or Pico W)
- USB cable (data capable, not power-only)
- Pico SDK installed
- CMake and ARM GCC toolchain

## Building

```bash
# Navigate to the test directory
cd test-blink

# Create and enter build directory
mkdir -p build
cd build

# Configure the build
cmake -DPICO_SDK_PATH=/path/to/pico-sdk ..

# Build the firmware
make -j4
```

The output will be `pico_blink_test.uf2` in the build directory.

## Flashing

1. Hold the BOOTSEL button on your Pico
2. Connect the Pico to your computer via USB while holding BOOTSEL
3. Release the BOOTSEL button
4. The Pico will appear as a USB mass storage device (RPI-RP2)
5. Copy the firmware:
   ```bash
   cp build/pico_blink_test.uf2 /path/to/RPI-RP2/
   ```
6. The Pico will automatically reboot and start running the test

## Expected Behavior

### Visual
- The onboard LED should blink on and off every 500ms

### Serial Output
Connect to the serial console to see detailed status:

```bash
# Using screen
screen /dev/ttyACM0 115200

# Using minicom
minicom -D /dev/ttyACM0 -b 115200

# Using picocom
picocom /dev/ttyACM0 -b 115200
```

You should see:
```
========================================
  Pico WH LED Blink Test
========================================
Firmware: v1.0
Board: Raspberry Pi Pico WH
Purpose: Hardware verification
========================================

Initializing WiFi chip...
WiFi chip initialized successfully!
Starting LED blink sequence...

[0] LED ON
[0] LED OFF
[1] LED ON
[1] LED OFF
...
--- Heartbeat: 10 blinks completed ---
```

## Troubleshooting

### LED Not Blinking

**Symptom**: No LED activity at all

**Possible causes**:
1. **Incorrect board type**: Ensure you're using Pico W or WH, not the original Pico (which doesn't have WiFi)
2. **Build configuration**: Verify `PICO_BOARD` is set to `pico_w` in CMakeLists.txt
3. **Hardware failure**: The WiFi chip or LED may be damaged
4. **Power issue**: Try a different USB cable or port

### No Serial Output

**Symptom**: LED blinks but no text appears in serial console

**Possible causes**:
1. **USB cable**: Using a power-only cable (no data lines)
2. **TinyUSB not initialized**: Check that pico-sdk's TinyUSB submodule is initialized:
   ```bash
   cd /path/to/pico-sdk
   git submodule update --init lib/tinyusb
   ```
3. **Serial port permissions**: Add yourself to the dialout group:
   ```bash
   sudo usermod -a -G dialout $USER
   # Log out and back in for changes to take effect
   ```
4. **Wrong serial device**: Try `/dev/ttyUSB0` instead of `/dev/ttyACM0`

### WiFi Chip Initialization Failed

**Symptom**: Serial output shows "ERROR: WiFi chip initialization failed!"

**Possible causes**:
1. **Incorrect board type**: Built for wrong board (original Pico instead of Pico W/WH)
2. **Hardware failure**: WiFi chip is damaged
3. **Corrupted firmware**: Try reflashing the firmware

## Success Criteria

✅ The test is successful if:
- The LED blinks regularly (every 500ms)
- Serial output shows successful WiFi chip initialization
- Serial output shows counting blink cycles

Once this test passes, your Pico WH hardware is confirmed working and ready for the full K3s node firmware.

## License

Apache License 2.0 - See LICENSE file for details

## Support

For issues specific to this test, see the main K3s Pico Node project documentation.
