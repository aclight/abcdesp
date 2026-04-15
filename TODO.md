# TODO

## Multi-Zone Support

Currently only Zone 1 is read and controlled. The Carrier Infinity protocol supports up to 8 zones — the data for zones 2–8 is already present in registers 3B02 (temperatures/humidity) and 3B03 (setpoints/fan modes) but is not parsed or exposed.

### Planned work
- Parse and expose temperature, humidity, and setpoints for all active zones
- Expose each zone as a separate climate entity (or sensor set)
- Allow per-zone control of setpoints and fan mode

## Hold / Schedule Behavior

When setpoints are changed from Home Assistant, the component currently forces the thermostat into permanent **hold** mode, overriding the built-in schedule indefinitely.

The physical thermostat behaves differently:
- Pressing temp up/down sets a **2-hour temporary hold** (adjustable at the thermostat)
- Changing mode (heat/cool/auto/off) does **not** trigger a hold

### Planned work
- Implement temporary hold with a configurable duration (default 2 hours) for setpoint changes
- Do not set hold when only changing mode or fan speed
- Provide a way to **clear hold** from Home Assistant, resuming the thermostat's built-in schedule
- Consider exposing a "Hold Status" sensor showing whether the system is in hold, and what type

## Additional Sensors

_(All planned sensors have been implemented.)_

## Entity Improvements

- **Heat stage labels** — map raw heat stage values (0–3) to meaningful labels (off/low/med/high) via a text sensor or HA template
- ~~**Single vs. dual setpoint by mode**~~ — done: single slider in heat/cool, dual in auto
- ~~**Fan-only mode**~~ — not supported by the Carrier Infinity protocol (no fan-only mode byte exists; modes are: heat, cool, auto, electric, heatpump_only, off)
- **Away/vacation preset** — map Carrier vacation hold to HA climate presets
- **Internalize Allow Control switch** — register the switch inside the component instead of requiring a separate `switch:` block in YAML

## Other

- **Bus device detection** — log which devices (thermostat, air handler, heat pump) are detected on the bus and warn about missing ones
- **Unique ID handling** — document behavior for users with multiple ESP32 units
