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

uart:
  id: updi_uart
  tx_pin: GPIO17
  rx_pin: GPIO16
  baud_rate: 115200
  parity: EVEN
  stop_bits: 2
  data_bits: 8
  rx_buffer_size: 512

updi_flasher:
  uart_id: updi_uart
```

Then browse to `http://<device>/updi`, choose your `.hex` (e.g. exported from
Arduino IDE with megaTinyCore: *Sketch > Export Compiled Binary*), and upload.

See `examples/` for full configs.

## Wiring

```
ESP32 TX ──[ 470R–1k ]──┬── ATtiny UPDI
ESP32 RX ───────────────┘
ESP32 GND ───────────────── ATtiny GND
ESP32 3V3 ───────────────── ATtiny VCC
```

TX and RX share the UPDI line, so every byte sent is echoed back; the component
discards the echo. Use a UART that is not used by the ESPHome logger.

## How it works

1. Break (0x00 @ 300 baud, 8E2), then UPDI init.
2. Chip erase via `NVMErase` key, reset.
3. Enter programming mode via `NVMProg` key, reset.
4. Write flash page by page (blank pages skipped), then read back and verify.
5. Leave programming mode and reset the target.

## Known caveats

- Targets tinyAVR 1-series with 16 KB flash. Other sizes need `FLASH_SIZE`, `PAGE_SIZE`, `FLASH_BASE` in `updi_flasher.h`.
- The baud change used for the break (`set_baud_rate` / `load_settings`) and the web-handler signatures vary between ESPHome versions; you may need small tweaks.
- **No authentication** on `/updi`: anyone on your network can flash the target.
- After flashing, UPDI is disabled until power-cycle (normal avrdude behaviour). If you want to reflash without power-cycling, remove the final `CTRLB` write in `flash_()`.
- Do not change the UPDI pin fuse to GPIO/RESET unless you have a high-voltage programmer.

## License

MIT
