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
- ~~Temporary hold~~ — `hold_duration_minutes` YAML option auto-clears hold after configured time (PR #14)

### Remaining work
- Consider supporting timed override via the 3B03 override fields (bytes 37-53) for native thermostat-managed temporary holds

## Additional Sensors

_(All planned sensors have been implemented.)_

## Entity Improvements

- ~~**Heat stage labels**~~ — done: `heat_stage_text_sensor` maps 0–3 to Off/Low/Med/High
- ~~**HP stage labels**~~ — done: `hp_stage_text_sensor` maps 0–2 to Off/Low/High (PR #12)
- ~~**Single vs. dual setpoint by mode**~~ — done: single slider in heat/cool, dual in auto
- ~~**Fan-only mode**~~ — not supported by the Carrier Infinity protocol (no fan-only mode byte exists; modes are: heat, cool, auto, electric, heatpump_only, off)
- ~~**Away/vacation preset**~~ — done: CLIMATE_PRESET_HOME/AWAY mapped to 3B04 vacation register
- ~~**Internalize Allow Control switch**~~ — done: AllowControlSwitch class registered inside the component
- ~~**Entity categories**~~ — done: allow_control_switch and clear_hold_button set to CONFIG, blower set to RUNNING device_class (PR #12)
- **Configurable vacation parameters** — vacation preset currently uses hardcoded values (7 days, 55–85°F, fan auto). Consider exposing as YAML config or number entities. Low priority — matches thermostat's native behavior.

## Home Assistant Dashboard

- ~~Ship a custom thermostat card or dashboard that reproduces the data displayed on the physical thermostat (current temp, humidity, setpoints, mode, outdoor temp, system status)~~ — done: `dashboard/hvac-dashboard.yaml` using built-in HA thermostat + glance + entities cards
- ~~Document which HA thermostat card works best and any quirks~~ — done: uses built-in `type: thermostat` card with `show_current_as_primary: true`; documented in README

## Logging

- Promote important failure conditions (repeated CRC failures, NAK'd writes) from `ESP_LOGW` to `ESP_LOGE` so they surface in HA's log viewer

## Other

- ~~**Bus device detection**~~ — done: logs which devices are detected on the bus after 60s and warns about missing ones
- ~~**Unique ID handling**~~ — done: documented in README
- ~~**secrets.yaml.example**~~ — done: template file for required secrets (PR #13)
