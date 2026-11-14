# XIAO ESP32-S3 Google TV Web Remote

A display-free Google TV remote for the Seeed Studio XIAO ESP32-S3. The board presents a USB NCM network adapter, assigns the computer an address with DHCP, and serves a self-contained web remote at `http://192.168.240.1`. BLE HID sends the controls to Google TV; Wi-Fi and external web dependencies are not used.

## Build and flash

Use ESP-IDF 6.1 or newer and a data-capable USB cable connected to the XIAO's USB-C port.

```powershell
idf.py set-target esp32s3
idf.py build
idf.py -p COMx flash
```

Replace `COMx` with the board's flashing port. If automatic reset does not enter the bootloader, hold the XIAO **BOOT** button while tapping **RESET**, then release **BOOT** and flash again.

## Use

1. Connect the XIAO to the computer by USB and wait a few seconds for the USB network adapter to obtain an address.
2. Open `http://192.168.240.1` in a browser. This link-local USB network does not provide internet access.
3. Put Google TV in **Settings → Remotes & Accessories → Add accessory** and select **XIAO GTV Remote**.
4. Use the on-screen controls or keyboard shortcuts. Power requires confirmation. The Saved devices section lists bonded hosts, disconnects the active host, switches to another saved host, and assigns persistent local display names. These labels are only used by the webapp and do not change the Bluetooth device name. Pair new device and confirmed removal of individual or all bonds are also available. Removing a local bond does not remove the host TV's own saved profile.

The HTTP interface is exposed only on the USB NCM interface. USB serial/JTAG logging is not available while TinyUSB is using the native USB peripheral; use the UART pins if firmware logs are needed.

The onboard user LED is solid while connected to a Google TV and blinks while disconnected. A frozen LED alongside an unresponsive webapp can help identify a firmware stall.

## Controls

Navigation/OK, Back, Home, Power, Volume Up/Down, Mute, Play/Pause, Rewind, Fast Forward, Previous, Next, and Stop use the same BLE HID report map as the touchscreen remote. Pair the new board separately because it has a different Bluetooth address.
