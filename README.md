# ABCDESP

ESPHome component that bridges ABCD bus HVAC systems to Home Assistant via RS-485,
impersonating a SAM (System Access Module).

## ⚠ Safety Warnings

- **NEVER connect to the C or D terminals** — they carry 24VAC and will destroy your ESP32 and RS-485 transceiver.
- Only connect to **A** (RS-485 data+) and **B** (RS-485 data−).
- If you have an existing SAM module on the bus, **disconnect it first**. Two devices at the same address (0x9201) will cause bus collisions.
- This interacts with your HVAC system using a reverse-engineered protocol. **Use at your own risk.**

## Parts List

| Part | Notes |
|------|-------|
| ESP32 dev board | Any ESP32-WROOM-32 based board (e.g. ESP32-DevKitC) |
| MAX485 module | RS-485 transceiver breakout (TTL level) |
| Thermostat wire | 2-conductor, to reach your ABCD terminals |
| 5V USB power supply | To power the ESP32 |

## Wiring

```
                     ┌──────────────┐
                     │   MAX485     │
  ABCD Bus ──A ────▶│ A            │
  ABCD Bus ──B ────▶│ B            │
                     │              │
         ESP32       │              │
  GPIO16 (RX) ◀─────│ RO           │
  GPIO17 (TX) ──────▶│ DI           │
  GPIO4  (DIR) ─────▶│ DE ┬ RE      │
                     │    └─(tied)  │
  3.3V ─────────────▶│ VCC          │
  GND ──────────────▶│ GND          │
                     └──────────────┘

  DE and RE pins on the MAX485 are bridged together and driven by GPIO4.
  HIGH = transmit mode, LOW = receive mode.
```

**Do NOT connect C or D from the ABCD bus. They are 24VAC power.**

## What It Exposes

| Entity | Type | Description |
|--------|------|-------------|
| HVAC | Climate | Mode (off/heat/cool/auto), fan (auto/low/med/high), heat+cool setpoints, current temperature |
| Outdoor Temperature | Sensor | Outdoor air temp in °F (from heat pump 3E01 if available, otherwise from thermostat 3B02) |
| Airflow CFM | Sensor | Air handler airflow in CFM |
| Blower Running | Binary Sensor | Whether the blower is running |
| Heat Stage | Sensor | Current heat stage (0=off, 1=low, 2=med, 3=high) |

## How It Works

The component impersonates a SAM module (address 0x9201) on the RS-485 bus:

1. **Polls** the thermostat (0x2001) every 5 seconds for registers 3B02 (current state) and 3B03 (zone settings).
2. **Snoops** responses from the air handler (0x4001) and heat pump (0x5001) as the thermostat polls them — no extra bus traffic needed.
3. **Writes** to the thermostat via 3B02 (mode changes) and 3B03 (setpoints, fan mode) using the SAM notification protocol with proper flag headers.
4. **ACKs** any WRITE frames the thermostat sends to the SAM address (typically 3B0E acknowledgments).

## Installation

1. Copy the `components/abcdesp/` folder into your ESPHome config directory.
2. Copy `abcdesp.yaml` to your ESPHome config directory.
3. Create a `secrets.yaml` with your WiFi credentials, API key, and OTA password.
4. Flash:

```bash
esphome run abcdesp.yaml
```

5. In Home Assistant, go to **Settings → Devices & Services** and adopt the new ESPHome device.

## Protocol References

- [Infinitive](https://github.com/acd/infinitive) — Go SAM impersonation (primary reference)
- [Infinitude Wiki](https://github.com/nebulous/infinitude/wiki/Infinity-Protocol-Main) — Protocol documentation
- [Gofinity](https://github.com/bvarner/gofinity) — Go library with hardware notes
- [Finitude](https://github.com/dulitz/finitude) — Python implementation

## SAM Module Note

A SAM (System Access Module) is a Carrier accessory that provides remote/internet access to the HVAC system. This component pretends to be one. If you have a physical SAM installed, you must disconnect it before using this component — two devices at the same bus address will corrupt communications.

Most residential systems with a standard thermostat, furnace, and heat pump do **not** have a SAM installed.
