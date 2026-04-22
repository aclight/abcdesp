# TODO

## Multi-Zone Support

Currently only Zone 1 is read and controlled. The Carrier Infinity protocol supports up to 8 zones — the data for zones 2–8 is already present in registers 3B02 (temperatures/humidity) and 3B03 (setpoints/fan modes) but is not parsed or exposed.

### Planned work
- Parse and expose temperature, humidity, and setpoints for all active zones
- Expose each zone as a separate climate entity (or sensor set)
- Allow per-zone control of setpoints and fan mode

## Remaining work
- **Configurable vacation parameters** — vacation preset currently uses hardcoded values (7 days, 60–80°F, fan auto). Consider exposing as YAML config or number entities. Low priority — matches thermostat's native behavior.
