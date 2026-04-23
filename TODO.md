# TODO

## Multi-Zone Support

Currently only Zone 1 is read and controlled. The Carrier Infinity protocol supports up to 8 zones — the data for zones 2–8 is already present in registers 3B02 (temperatures/humidity) and 3B03 (setpoints/fan modes) but is not parsed or exposed.

### Planned work
- Parse and expose temperature, humidity, and setpoints for all active zones
- Expose each zone as a separate climate entity (or sensor set)
- Allow per-zone control of setpoints and fan mode

## Remaining work
- ~~**Configurable vacation parameters**~~ — done: `vacation_days_number`, `vacation_min_temp_number`, and `vacation_max_temp_number` entities configure vacation settings before activating Away preset; `parse_vacation` updates entities from thermostat state when vacation is active; defaults to 7 days, 60–80°F if entities not configured
