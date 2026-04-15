# TODO

## Multi-Zone Support

Currently only Zone 1 is read and controlled. The Carrier Infinity protocol supports up to 8 zones — the data for zones 2–8 is already present in registers 3B02 (temperatures/humidity) and 3B03 (setpoints/fan modes) but is not parsed or exposed.

### Planned work
- Parse and expose temperature, humidity, and setpoints for all active zones
- Expose each zone as a separate climate entity (or sensor set)
- Allow per-zone control of setpoints and fan mode

## Hold / Schedule Behavior

When setpoints are changed from Home Assistant, the component places the thermostat into permanent **hold** mode, overriding the built-in schedule.

### Done
- ~~Clear hold from HA~~ — Clear Hold button entity sends 3B03 write clearing the hold flag
- ~~Hold status sensor~~ — Hold Active binary sensor shows whether zone 1 is in hold
- ~~Write feedback~~ — state updates are no longer optimistic; the next poll confirms the thermostat accepted changes
- Mode and fan changes no longer set hold (only setpoint changes trigger hold)

### Remaining work
- Implement temporary hold with a configurable duration (default 2 hours) for setpoint changes
- Consider supporting timed override via the 3B03 override fields (bytes 37-53)

## Additional Sensors

_(All planned sensors have been implemented.)_

## Entity Improvements

- **Heat stage labels** — map raw heat stage values (0–3) to meaningful labels (off/low/med/high) via a text sensor or HA template
- ~~**Single vs. dual setpoint by mode**~~ — done: single slider in heat/cool, dual in auto
- ~~**Fan-only mode**~~ — not supported by the Carrier Infinity protocol (no fan-only mode byte exists; modes are: heat, cool, auto, electric, heatpump_only, off)
- **Away/vacation preset** — map Carrier vacation hold to HA climate presets
- **Internalize Allow Control switch** — register the switch inside the component instead of requiring a separate `switch:` block in YAML

## Other

- ~~**Bus device detection**~~ — done: logs which devices are detected on the bus after 60s and warns about missing ones
- ~~**Unique ID handling**~~ — done: documented in README
