# Hardware Testing Plan — Core Functions (Mode, Fan, Sensors, Communication)

These tests cover the base functionality of the component that is **not** exercised by the hold or vacation test plans. They **must be run on real Carrier Infinity equipment**.

Monitor the thermostat display and ESPHome logs closely.

## Prerequisites

- ESP32 connected to Carrier Infinity bus and communicating (Comms OK = true)
- Allow Control switch: ON
- ESPHome logs visible at DEBUG level
- Home Assistant dashboard open showing all entities
- System in a stable state (no active hold, no vacation)

---

## A. Mode Changes

### 1. Switch mode: Heat → Cool

**Steps:**
1. Set climate mode to Heat from HA (if not already)
2. Wait for thermostat display to confirm Heat mode
3. Switch climate mode to Cool from HA

**Expected:**
- Thermostat display shows Cool mode
- Climate entity mode updates to `cool`
- Action updates appropriately (idle or cooling depending on setpoint)

### 2. Switch mode: Cool → Auto (Heat/Cool)

**Steps:**
1. With mode set to Cool, switch to Heat/Cool (Auto) from HA

**Expected:**
- Thermostat display shows Auto mode
- Climate entity mode updates to `heat_cool`
- Both heat and cool setpoints visible in HA

### 3. Switch mode: Auto → Off

**Steps:**
1. With mode set to Auto, switch to Off from HA

**Expected:**
- Thermostat display shows system Off
- Climate entity mode updates to `off`
- Climate action → `off`
- Blower should stop (may take a few seconds for fan-off delay)

### 4. Switch mode: Off → Heat

**Steps:**
1. With mode Off, switch to Heat from HA

**Expected:**
- Thermostat display shows Heat mode
- Climate entity updates to `heat`

### 5. Mode change from thermostat reflects in HA

**Steps:**
1. Change mode on the physical thermostat (e.g., Heat → Cool)
2. Wait for a poll cycle (~5 seconds)

**Expected:**
- Climate entity in HA updates to match thermostat selection
- No writes sent by the ESP (read-only sync)

---

## B. Fan Mode Changes

### 6. Cycle fan modes: Auto → Low → Med → High → Auto

**Steps:**
1. Set fan mode to Low from HA; verify thermostat shows Low
2. Set fan mode to Medium from HA; verify thermostat shows Med
3. Set fan mode to High from HA; verify thermostat shows High
4. Set fan mode to Auto from HA; verify thermostat shows Auto

**Expected (each step):**
- Thermostat display matches the selected fan speed
- Climate entity `fan_mode` matches selection
- If system is actively heating/cooling, blower speed changes audibly

### 7. Fan mode change from thermostat reflects in HA

**Steps:**
1. Change fan mode on the physical thermostat
2. Wait for a poll cycle (~5 seconds)

**Expected:**
- Climate entity `fan_mode` in HA updates to match

---

## C. Temperature Sensors

### 8. Current temperature reads correctly

**Steps:**
1. Compare thermostat display current temperature with the climate entity `current_temperature` in HA
2. If you have an independent thermometer, cross-check

**Expected:**
- Values match within ±1°F (thermostat and HA show the same reading)

### 9. Outdoor temperature sensor

**Steps:**
1. Check `sensor.abcdesp_outdoor_temperature` in HA
2. Compare with the thermostat display (if thermostat shows outdoor temp) or weather forecast

**Expected:**
- Value is a plausible outdoor temperature for current conditions
- Updates periodically (within ~15 seconds of a real change)
- If no heat pump on the bus, this comes from the thermostat (3B02); if heat pump present, from 3E01

### 10. Indoor humidity sensor

**Steps:**
1. Check `sensor.abcdesp_indoor_humidity` in HA
2. Compare with the thermostat display humidity (if shown)

**Expected:**
- Value is in 0–100% range, plausible for current indoor conditions
- Matches thermostat reading within ±1%

### 11. HP coil temperature sensor

**Requires:** Heat pump on the RS-485 bus.

**Steps:**
1. Check `sensor.abcdesp_hp_coil_temperature` in HA
2. With system running in heat or cool, verify value is plausible:
   - Heating: coil temp should be warmer than outdoor
   - Cooling: coil temp should be colder than indoor

**Expected:**
- Value updates when system is actively running
- Returns to near-ambient when system is idle

### 12. Airflow CFM sensor

**Requires:** Air handler on the RS-485 bus.

**Steps:**
1. Check `sensor.abcdesp_airflow_cfm` while system is idle → should be 0
2. Trigger heating or cooling; wait for blower to start
3. Verify CFM reads a plausible value (typically 400–2000 CFM depending on system size and fan speed)

**Expected:**
- 0 when blower is off
- Positive value when blower is running
- Higher values at higher fan speeds (Low < Med < High)

---

## D. Stage Sensors

### 13. Heat stage sensor

**Requires:** Air handler on the RS-485 bus, system in Heat mode.

**Steps:**
1. Set mode to Heat with a setpoint well above current temperature
2. Wait for system to call for heat

**Expected:**
- `sensor.abcdesp_heat_stage` updates from 0 to 1 (or higher for multi-stage)
- `text_sensor.abcdesp_heat_stage_label` updates: "Off" → "Low" (→ "Med" → "High" if multi-stage)
- When setpoint is satisfied and system stops, stage returns to 0 / "Off"

### 14. HP stage sensor

**Requires:** Heat pump on the RS-485 bus, system in Heat or Cool mode.

**Steps:**
1. Trigger a heating or cooling call
2. Wait for heat pump to start

**Expected:**
- `sensor.abcdesp_hp_stage` updates from 0 to 1 (or 2 for high stage)
- `text_sensor.abcdesp_hp_stage_label` updates: "Off" → "Low" (→ "High")
- Stage returns to 0 / "Off" when call ends

---

## E. Binary Sensors

### 15. Blower running sensor

**Steps:**
1. With system idle, verify `binary_sensor.abcdesp_blower_running` = OFF
2. Trigger a heating or cooling call
3. Wait for blower to start (there may be a startup delay)

**Expected:**
- Sensor flips to ON when blower engages
- Flips back to OFF when blower stops (may lag by fan-off delay, typically 60–90 seconds)

### 16. Hold Active sensor

**Steps:**
1. Clear any holds; verify `binary_sensor.abcdesp_hold_active` = OFF
2. Change a setpoint from HA (this should activate a hold)
3. Verify sensor = ON
4. Press "Clear Hold" button
5. Verify sensor = OFF

**Expected:**
- State transitions match hold activation and clearing
- Also changes if hold is set or cleared from the physical thermostat

### 17. Communication OK sensor

**Steps:**
1. With system running normally, verify `binary_sensor.abcdesp_communication_ok` = ON
2. (Optional/destructive) Disconnect the RS-485 data line briefly
3. Wait >30 seconds

**Expected:**
- Sensor = ON during normal operation
- Sensor → OFF if no response received within 30 seconds
- Sensor → ON again when communication resumes

---

## F. Climate Action State

### 18. Action reflects current system activity

**Steps:**
1. Set mode to Off → action = `off`
2. Set mode to Heat, setpoint well above current temp → wait for system to engage → action = `heating`
3. Observe blower running after heat call stops → action = `fan` (if blower is on but not heating/cooling)
4. After blower stops → action = `idle`
5. Set mode to Cool, setpoint well below current temp → wait → action = `cooling`

**Expected:**
- Action state correctly tracks: off, heating, cooling, fan, idle
- Transitions happen within one poll cycle (~5 seconds) of the physical state change

---

## G. Setpoint Changes

### 19. Change heat setpoint

**Steps:**
1. Set mode to Heat
2. Change target temperature from HA (e.g., 70°F)

**Expected:**
- Thermostat display shows new setpoint
- Climate entity `target_temperature` matches

### 20. Change cool setpoint

**Steps:**
1. Set mode to Cool
2. Change target temperature from HA (e.g., 76°F)

**Expected:**
- Thermostat display shows new setpoint
- Climate entity `target_temperature` matches

### 21. Change dual setpoints in Auto mode

**Steps:**
1. Set mode to Auto (Heat/Cool)
2. Change heat setpoint (target_temperature_low) to 68°F
3. Change cool setpoint (target_temperature_high) to 76°F

**Expected:**
- Thermostat display shows both setpoints
- Climate entity shows correct `target_temperature_low` and `target_temperature_high`

### 22. Setpoint change from thermostat reflects in HA

**Steps:**
1. Change a setpoint on the physical thermostat
2. Wait for a poll cycle (~5 seconds)

**Expected:**
- Climate entity in HA updates to match the thermostat setpoint

---

## H. Control Gating

### 23. All writes blocked when Allow Control is OFF

**Steps:**
1. Turn Allow Control switch OFF
2. Try each of the following from HA:
   - Change mode
   - Change fan mode
   - Change setpoint
   - Set Away preset
3. Verify none produce bus writes

**Expected (each attempt):**
- No write sent to the bus
- Log: "Control blocked: Allow Control switch is OFF"
- Thermostat display unchanged
- Read-only sensor updates continue normally

### 24. Allow Control toggle

**Steps:**
1. Turn Allow Control OFF → verify writes blocked (test 23)
2. Turn Allow Control ON → change a setpoint → verify write succeeds
3. Reboot ESP32 → verify Allow Control defaults to OFF

**Expected:**
- Switch persists value across reboots (should restore last state, but defaults to OFF on first boot)
- Writes only occur when switch is ON

---

## I. Read-Only Mode (First Boot / Default State)

> Read-only mode is the **default state** on first boot — Allow Control is OFF.
> These tests validate the full monitoring experience with zero writes to the bus.

### 27. First-boot read-only experience

**Steps:**
1. Flash a fresh ESP32 (or erase flash: ESPHome → Clean Build Files → Install)
2. Let the device boot and connect to HA
3. Do **not** touch Allow Control — it should default to OFF

**Expected:**
- `switch.abcdesp_allow_control` = OFF
- Climate entity populates: current temp, mode, fan mode, setpoints all read from thermostat
- All configured sensors begin reporting values within ~15 seconds
- Comms OK → ON
- ESPHome logs show read requests only — no 0x0C (write) function codes

### 28. All sensors update in read-only mode

**Steps:**
1. Confirm Allow Control is OFF
2. Observe the dashboard for 2–3 minutes

**Expected (check each):**
- `climate.abcdesp_hvac` — current temp, mode, fan mode, setpoints populated
- `sensor.abcdesp_outdoor_temperature` — plausible value
- `sensor.abcdesp_indoor_humidity` — plausible value (0–100%)
- `binary_sensor.abcdesp_blower_running` — matches actual blower state
- `sensor.abcdesp_airflow_cfm` — reads 0 when idle, positive when running (if air handler on bus)
- `sensor.abcdesp_heat_stage` / `text_sensor.abcdesp_heat_stage_label` — updates if system is heating
- `sensor.abcdesp_hp_coil_temperature` / `sensor.abcdesp_hp_stage` — updates if heat pump on bus
- `binary_sensor.abcdesp_hold_active` — matches thermostat hold state
- `sensor.abcdesp_hold_time_remaining` — shows countdown if timed hold active
- `binary_sensor.abcdesp_communication_ok` — ON

### 29. Thermostat-initiated mode change reflects in read-only mode

**Steps:**
1. Confirm Allow Control is OFF
2. Change mode on the physical thermostat (e.g., Heat → Cool)
3. Wait one poll cycle (~5 seconds)

**Expected:**
- Climate entity in HA updates to match
- No writes sent by the ESP (verify in logs — no "Sending write" messages)

### 30. Thermostat-initiated setpoint change reflects in read-only mode

**Steps:**
1. Confirm Allow Control is OFF
2. Change a setpoint on the physical thermostat
3. Wait one poll cycle

**Expected:**
- Climate entity setpoint in HA updates to match thermostat
- No bus writes

### 31. Thermostat-initiated hold reflects in read-only mode

**Steps:**
1. Confirm Allow Control is OFF
2. Set a hold from the physical thermostat (change setpoint and confirm hold on display)
3. Wait one poll cycle

**Expected:**
- `binary_sensor.abcdesp_hold_active` → ON
- `sensor.abcdesp_hold_time_remaining` updates if timed hold
- No bus writes from ESP

### 32. Thermostat-initiated vacation reflects in read-only mode

**Steps:**
1. Confirm Allow Control is OFF
2. Activate vacation mode from the physical thermostat
3. Wait for a 3B04 poll cycle (~15 seconds)

**Expected:**
- Climate entity preset → "Away"
- Vacation number entities update to reflect thermostat values (days, min/max temps)
- No bus writes from ESP

### 33. HA control attempts are silently blocked in read-only mode

**Steps:**
1. Confirm Allow Control is OFF
2. Attempt each from HA:
   - Change mode (e.g., Heat → Cool)
   - Change fan mode
   - Change setpoint
   - Set preset to "Away"
   - Press "Clear Hold"
   - Set "Set Hold Time" to 60

**Expected (each attempt):**
- No write sent to bus
- Log: "Control blocked: Allow Control switch is OFF" (or specific variant)
- Thermostat display unchanged
- Entity state in HA does **not** change (climate stays at thermostat values)

### 34. Extended read-only stability (soak test)

**Steps:**
1. Confirm Allow Control is OFF
2. Leave the system running for at least 1 hour
3. Periodically check the dashboard

**Expected:**
- All sensor values continue updating
- Comms OK stays ON
- No errors in ESPHome logs (WARN or ERROR level)
- No writes sent (grep logs for "Sending write" — should be zero)
- Climate entity stays in sync with thermostat through any mode/setpoint changes made at the thermostat

---

## J. Communication Resilience

### 35. Recovery after brief bus interruption

**Steps:**
1. Briefly disconnect then reconnect the RS-485 data lines (< 5 seconds)
2. Watch ESPHome logs and Comms OK sensor

**Expected:**
- Comms OK may briefly flicker but should not drop (30-second timeout)
- Polling resumes normally after reconnection
- No error codes on thermostat

### 36. CRC failure logging

**Steps:**
1. Monitor ESPHome logs at DEBUG level during normal operation
2. Check for any CRC failure messages

**Expected:**
- Occasional CRC failures are normal on a noisy bus (logged at DEBUG)
- After 10 consecutive CRC failures, an ERROR-level log appears
- System continues operating despite CRC failures (frames are just discarded)

---

## Failure Criteria

If any of the following occur, **stop testing and investigate**:
- Thermostat shows error codes or enters fault state
- HVAC system starts/stops unexpectedly after a command
- Sustained NAK responses to writes (check logs)
- Communication lost and does not recover after 60 seconds
- Thermostat display persistently disagrees with HA entity state
- Blower runs continuously with no heating/cooling call
