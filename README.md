# ABCDESP

ESPHome component that bridges ABCD bus HVAC systems to Home Assistant via RS-485,
impersonating a SAM (System Access Module).

## ⚠ DO NOT USE THIS PROJECT
At this point the code is completely untested on a real device. You should definitely not use it.

## ⚠ Safety Warnings

- **NEVER connect to the C or D terminals** — they carry 24VAC and will destroy your ESP32 and RS-485 transceiver.
- Only connect to **A** (RS-485 data+) and **B** (RS-485 data−).
- If you have an existing SAM module on the bus, **disconnect it first**. Two devices at the same address (0x9201) will cause bus collisions.
- This interacts with your HVAC system using a reverse-engineered protocol. **Use at your own risk.**

## Parts List

| Part | Mouser # | Notes |
|------|----------|-------|
| M5Stack AtomS3 | 170-C124 | ESP32-S3 controller (plugs into the RS485 base) |
| M5Stack ATOMIC RS485 Base | 170-A131 | RS485 transceiver with built-in DC-DC (powers the AtomS3) |
| Thermostat wire | — | 2-conductor, to reach your ABCD terminals |

## Wiring

The AtomS3 plugs directly into the ATOMIC RS485 Base — no jumper wires needed between the two modules. The RS485 Base maps the bus signals to the AtomS3 automatically:

| Signal | AtomS3 GPIO | Notes |
|--------|-------------|-------|
| RS485 RX | GPIO5 (pin 3 on base) | Receive data from bus |
| RS485 TX | GPIO6 (pin 5 on base) | Transmit data to bus |
| Direction | — | Auto-direction (hardware on RS485 Base, no GPIO needed) |

### Screw Terminal (VH-3.96 4P on RS485 Base)

Connect your ABCD bus wires and power to the 4-pin screw terminal on the RS485 Base. Refer to the I/O sticker included with the base for the exact pin order:

| Terminal | Connect to | Notes |
|----------|-----------|-------|
| **A** | ABCD Bus terminal **A** | RS-485 Data+ |
| **B** | ABCD Bus terminal **B** | RS-485 Data− |
| **GND** | Power supply ground | Common ground |
| **VIN** | DC power supply **+** | 4.5–36V DC input (powers the AtomS3 via built-in DC-DC) |

### Power

You have two options for powering the device:

1. **Screw terminal VIN** (recommended for installation): Supply 4.5–36V DC to the VIN terminal. 12V DC works well. The RS485 Base's built-in DC-DC (AOZ1282CI) steps it down to 5V for the AtomS3.
2. **USB-C** (for bench testing): Plug USB-C directly into the AtomS3. Do **not** supply VIN and USB-C power simultaneously.

> **Do NOT connect to the C or D terminals on the ABCD bus. They carry 24VAC and will destroy the device.**

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
| HP Stage Label | Text Sensor | Heat pump stage as text (Off/Low/High) |
| Communication OK | Binary Sensor | Whether the ESP32 is receiving responses from the thermostat (goes offline after 30s of no response) |
| Hold Active | Binary Sensor | Whether zone 1 is in hold mode (schedule overridden) |
| Hold Time Remaining | Sensor | Minutes remaining on a timed hold (0 when not on timed override) |
| Clear Hold | Button | Clears hold on zone 1, resuming the thermostat's built-in schedule (requires Allow Control ON) |

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

When you change setpoints from Home Assistant, the component places the thermostat into **hold** mode. This means the thermostat's built-in schedule is overridden.

By default, hold is **permanent** (matching the thermostat's native behavior when you adjust setpoints at the unit). To use a **temporary hold** that automatically clears after a set time, add the `hold_duration_minutes` option:

```yaml
abcdesp:
  hold_duration_minutes: 120  # auto-clear hold after 2 hours (0 = permanent, default)
```

When a temporary hold is set, the component writes the duration to the thermostat's native timed override fields (3B03 bytes 37-53) so the thermostat manages the countdown — this survives ESP32 reboots and the thermostat may display the remaining time. An ESP-side timer is also maintained as a fallback in case the thermostat doesn't support native timed override.

The **Hold Time Remaining** sensor reports the minutes remaining on a timed hold (as reported by the thermostat). It reads 0 when there is no timed override active.

To clear a hold manually at any time, press the **Clear Hold** button in Home Assistant (requires the Allow Control switch to be ON). You can also clear the hold at the thermostat itself.

The **Hold Active** binary sensor shows whether zone 1 is currently in hold mode.

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
3. Copy `secrets.yaml.example` to `secrets.yaml` and fill in your WiFi credentials, API key, and OTA password.
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

If you have multiple Carrier Infinity systems (e.g. separate upstairs/downstairs units), each needs its own AtomS3 + RS485 Base wired to its own ABCD bus. Give each a unique `name:` in its YAML config:

```yaml
esphome:
  name: abcdesp-upstairs    # unique per device
  friendly_name: HVAC Upstairs
```

Each device will appear as a separate ESPHome integration in Home Assistant with its own set of entities. The `name` field determines the device's mDNS hostname and HA entity ID prefix.

## Dashboard

A ready-to-use Home Assistant dashboard is included in `dashboard/hvac-dashboard.yaml`. It uses only built-in HA cards (no HACS or custom cards required) and shows:

- **Thermostat card** — ring dial with current temperature, setpoints, mode, fan, and preset controls
- **Conditions** — outdoor temperature, indoor humidity, and HP coil temperature at a glance
- **System status** — blower, heat stage, HP stage, airflow CFM
- **Controls** — hold status, clear hold, allow control, communication health

To use it:

1. In Home Assistant, go to **Settings → Dashboards → Add Dashboard**
2. Create a new dashboard, open it, and switch to YAML mode (three-dot menu → Raw configuration editor)
3. Paste the contents of `dashboard/hvac-dashboard.yaml`

Entity IDs assume `esphome.name: abcdesp`. If you changed your device name, find/replace `abcdesp_` with your device name + underscore.
