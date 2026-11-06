# esphome-updi-flasher

ESPHome external component that turns an ESP32 into a **wireless UPDI programmer**
for tinyAVR 1-series chips (built for **ATtiny1616**: 16 KB flash, 64-byte pages).
Open a web page on the ESP32, upload a `.hex`, and it erases, writes, verifies and
resets the ATtiny. No PC involved except for building the hex.

> **Status: experimental / untested.** Written from the UPDI protocol (pyupdi / pymcuprog
> logic) without hardware testing. Issues and PRs welcome.

## Usage

```yaml
external_components:
  - source: github://mahipat99/esphome-updi-flasher@main
    components: [updi_flasher]

updi_flasher:
  pin: GPIO17        # any output-capable GPIO
  uart_num: 1        # optional, default 1
  baud_rate: 115200
```

Then browse to `http://<device>/updi`, choose your `.hex` (e.g. exported from
Arduino IDE with megaTinyCore: *Sketch > Export Compiled Binary*), and upload.

See `examples/` for full configs.

## Options

| Option | Default | Meaning |
|---|---|---|
| `pin` | required | GPIO used as the single UPDI wire (`GPIO21`, `TX`, ... all accepted). |
| `uart_num` | `1` | Which of the chip's internal UART peripherals generates the UPDI signal. Independent of the pin (the GPIO matrix maps any pin to any UART). Don't pick the one the ESPHome logger uses (UART0 by default); this is checked at compile time, as is the chip's UART count. `uart_num: 0` is allowed when the logger is moved to USB (`USB_SERIAL_JTAG` / `USB_CDC`) or disabled. |
| `baud_rate` | `115200` | UPDI baud rate. |

UART peripherals per chip: ESP32 / S3 = 3 (0-2), S2 / C2 / C3 / C5 / C6 / H2 = 2 (0-1), P4 = 5 (0-4).

## Wiring

```
ESP32 GPIO17 ───────────┬── ATtiny UPDI
                        └── 4.7k–10k pull-up to 3V3 (recommended)
ESP32 GND ───────────────── ATtiny GND
ESP32 3V3 ───────────────── ATtiny VCC
```

The pin is configured open-drain and the hardware UART's TX and RX are both routed to
it through the GPIO matrix, so no series resistor is needed. Each byte sent is echoed
back; the component discards the echo. Use a UART that is not used by the ESPHome logger.
**ESP-IDF framework only.**

## How it works

1. Break (0x00 @ 300 baud, 8E2), then UPDI init.
2. Chip erase via `NVMErase` key, reset.
3. Enter programming mode via `NVMProg` key, reset.
4. Write flash page by page (blank pages skipped), then read back and verify.
5. Leave programming mode and reset the target.

## Known caveats

- Targets tinyAVR 1-series with 16 KB flash. Other sizes need `FLASH_SIZE`, `PAGE_SIZE`, `FLASH_BASE` in `updi_flasher.h`.
- Web-handler signatures vary between ESPHome versions; you may need small tweaks.
- **No authentication** on `/updi`: anyone on your network can flash the target.
- After flashing, UPDI is disabled until power-cycle (normal avrdude behaviour). If you want to reflash without power-cycling, remove the final `CTRLB` write in `flash_()`.
- Do not change the UPDI pin fuse to GPIO/RESET unless you have a high-voltage programmer.

## License

MIT
