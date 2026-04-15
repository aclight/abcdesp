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

- **Indoor humidity** — already parsed from 3B02 but not published as a sensor entity
- **Heat pump coil temperature** — already parsed from 3E01 but not published
- **Heat pump stage** — already parsed from 3E02 but not published
- **Communication health** — add a "Last Successful Poll" timestamp sensor and a "Communication OK" binary sensor that goes false if no successful response is received within a configurable timeout

## Entity Improvements

- **Fahrenheit/Celsius** — declare native temperature unit so Home Assistant can convert for users configured for Celsius
- **Heat stage labels** — map raw values (0–3) to meaningful labels (off/low/med/high) via a text sensor or enum
- **Blower device_class** — fix invalid `device_class: running` on the blower binary sensor

## Other

- **Web server** — add ESPHome `web_server` component for local debugging without Home Assistant
- **Unique ID handling** — document behavior for users with multiple ESP32 units
