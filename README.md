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
| HVAC | Climate | Mode (off/heat/cool/auto), fan (auto/low/med/high), setpoints, current temperature and humidity |
| Allow Control | Switch | Enables HVAC control from HA (default: OFF — see [Read-Only Mode](#read-only-mode)) |
| Outdoor Temperature | Sensor | Outdoor air temp in °F (from heat pump 3E01 if available, otherwise from thermostat 3B02) |
| Indoor Humidity | Sensor | Indoor relative humidity (%) from thermostat |
| Airflow CFM | Sensor | Air handler airflow in CFM |
| Blower Running | Binary Sensor | Whether the blower is running |
| Heat Stage | Sensor | Current heat stage (0=off, 1=low, 2=med, 3=high) |
| HP Coil Temperature | Sensor | Heat pump coil temperature in °F |
| HP Stage | Sensor | Heat pump compressor stage |
| Communication OK | Binary Sensor | Whether the ESP32 is receiving responses from the thermostat (goes offline after 30s of no response) |

> **Note:** Only **Zone 1** is currently supported. Multi-zone systems will only see data for the first zone. See [TODO.md](TODO.md) for planned multi-zone support.

> **Temperature units:** All sensors report in °F internally. Home Assistant automatically converts values to match your configured unit system (°C or °F), so temperatures will display correctly regardless of your HA settings.

## How It Works

The component impersonates a SAM module (address 0x9201) on the RS-485 bus:

1. **Polls** the thermostat (0x2001) every 5 seconds for registers 3B02 (current state) and 3B03 (zone settings).
2. **Snoops** responses from the air handler (0x4001) and heat pump (0x5001) as the thermostat polls them — no extra bus traffic needed.
3. **Writes** to the thermostat via 3B02 (mode changes) and 3B03 (setpoints, fan mode) using the SAM notification protocol with proper flag headers.
4. **ACKs** any WRITE frames the thermostat sends to the SAM address (typically 3B0E acknowledgments).

## Read-Only Mode

By default the component starts in **read-only mode** — it monitors the HVAC bus but will not send any control commands. This lets you verify everything is working before allowing HA to change your HVAC settings.

To enable control, toggle the **Allow Control** switch in Home Assistant. The switch defaults to OFF and remembers its state across reboots (it will stay OFF unless you explicitly turn it ON).

## Hold Behavior

When you change setpoints from Home Assistant, the component places the thermostat into **hold** mode. This means the thermostat's built-in schedule is overridden until the hold is cleared at the thermostat itself. This is a known limitation — see [TODO.md](TODO.md) for planned improvements.

## Installation

1. Add the following to your ESPHome YAML config to pull the component directly from GitHub:

```yaml
external_components:
  - source:
      type: git
      url: https://github.com/aclight/abcdesp
      ref: main
    components: [abcdesp]
```

2. See `abcdesp.yaml` in this repository for a complete example configuration.
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

## Multiple ESP32 Units

If you have multiple Carrier Infinity systems (e.g. separate upstairs/downstairs units), each needs its own ESP32 + MAX485 wired to its own ABCD bus. Give each a unique `name:` in its YAML config:

```yaml
esphome:
  name: abcdesp-upstairs    # unique per device
  friendly_name: HVAC Upstairs
```

Each device will appear as a separate ESPHome integration in Home Assistant with its own set of entities. The `name` field determines the device's mDNS hostname and HA entity ID prefix.
