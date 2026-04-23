# Hardware Testing Plan — Core Functions

These tests cover the base functionality of the component. They **must be run on real Carrier Infinity equipment**.
Monitor the thermostat display and ESPHome logs closely.

> **How to use this file:** Copy into a GitHub Issue (checkboxes become interactive) or edit directly.
> Mark each test PASS or FAIL, and fill in the Actual / Notes field with observations.
> If any test fails, paste the filled-in section back to Copilot for diagnosis.

## Prerequisites

- [ ] ESP32 connected to Carrier Infinity bus and communicating (Comms OK = true)
- [ ] ESPHome logs visible at DEBUG level
- [ ] Home Assistant dashboard open showing all entities
- [ ] System in a stable state (no active hold, no vacation)

**Firmware version:** _______________
**Date tested:** _______________
**Tester:** _______________

---

## A. Read-Only Mode (First Boot / Default State)

> Read-only mode is the **default state** on first boot — Allow Control is OFF.
> Run this section first. No writes should be sent to the bus.

---

### 1. First-boot read-only experience

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

**Result:**
- [ ] PASS
- [ ] FAIL

**Actual / Notes:**
> 

---

### 2. All sensors update in read-only mode

**Steps:**
1. Confirm Allow Control is OFF
2. Observe the dashboard for 2–3 minutes

**Expected (check each):**
- [ ] `climate.abcdesp_hvac` — current temp, mode, fan mode, setpoints populated
- [ ] `sensor.abcdesp_outdoor_temperature` — plausible value
- [ ] `sensor.abcdesp_indoor_humidity` — plausible value (0–100%)
- [ ] `binary_sensor.abcdesp_blower_running` — matches actual blower state
- [ ] `sensor.abcdesp_airflow_cfm` — 0 when idle, positive when running (if air handler on bus)
- [ ] `sensor.abcdesp_heat_stage` / `text_sensor.abcdesp_heat_stage_label` — updates if heating
- [ ] `sensor.abcdesp_hp_coil_temperature` / `sensor.abcdesp_hp_stage` — updates if HP on bus
- [ ] `binary_sensor.abcdesp_hold_active` — matches thermostat hold state
- [ ] `sensor.abcdesp_hold_time_remaining` — shows countdown if timed hold active
- [ ] `binary_sensor.abcdesp_communication_ok` — ON

**Result:**
- [ ] PASS
- [ ] FAIL

**Actual / Notes:**
> 

---

### 3. Thermostat-initiated mode change reflects in read-only mode

**Steps:**
1. Confirm Allow Control is OFF
2. Change mode on the physical thermostat (e.g., Heat → Cool)
3. Wait one poll cycle (~5 seconds)

**Expected:**
- Climate entity in HA updates to match
- No writes sent (verify in logs — no "Sending write" messages)

**Result:**
- [ ] PASS
- [ ] FAIL

**Actual / Notes:**
> 

---

### 4. Thermostat-initiated setpoint change reflects in read-only mode

**Steps:**
1. Confirm Allow Control is OFF
2. Change a setpoint on the physical thermostat
3. Wait one poll cycle

**Expected:**
- Climate entity setpoint in HA updates to match thermostat
- No bus writes

**Result:**
- [ ] PASS
- [ ] FAIL

**Actual / Notes:**
> 

---

### 5. Thermostat-initiated hold reflects in read-only mode

**Steps:**
1. Confirm Allow Control is OFF
2. Set a hold from the physical thermostat (change setpoint and confirm hold on display)
3. Wait one poll cycle

**Expected:**
- `binary_sensor.abcdesp_hold_active` → ON
- `sensor.abcdesp_hold_time_remaining` updates if timed hold
- No bus writes from ESP

**Result:**
- [ ] PASS
- [ ] FAIL

**Actual / Notes:**
> 

---

### 6. Thermostat-initiated vacation reflects in read-only mode

**Steps:**
1. Confirm Allow Control is OFF
2. Activate vacation mode from the physical thermostat
3. Wait for a 3B04 poll cycle (~15 seconds)

**Expected:**
- Climate entity preset → "Away"
- Vacation number entities update to reflect thermostat values (days, min/max temps)
- No bus writes from ESP

**Result:**
- [ ] PASS
- [ ] FAIL

**Actual / Notes:**
> 

---

### 7. HA control attempts are silently blocked in read-only mode

**Steps:**
1. Confirm Allow Control is OFF
2. Attempt each from HA:
   - [ ] Change mode (e.g., Heat → Cool) — blocked?
   - [ ] Change fan mode — blocked?
   - [ ] Change setpoint — blocked?
   - [ ] Set preset to "Away" — blocked?
   - [ ] Press "Clear Hold" — blocked?
   - [ ] Set "Set Hold Time" to 60 — blocked?

**Expected (each attempt):**
- No write sent to bus
- Log: "Control blocked: Allow Control switch is OFF" (or specific variant)
- Thermostat display unchanged
- Entity state in HA does **not** change

**Result:**
- [ ] PASS
- [ ] FAIL

**Actual / Notes:**
> 

---

### 8. Extended read-only stability (soak test)

**Steps:**
1. Confirm Allow Control is OFF
2. Leave the system running for at least 1 hour
3. Periodically check the dashboard

**Expected:**
- All sensor values continue updating
- Comms OK stays ON
- No errors in ESPHome logs (WARN or ERROR level)
- No writes sent (grep logs for "Sending write" — should be zero)
- Climate entity stays in sync with thermostat through any changes made at the thermostat

**Duration tested:** _______________ minutes

**Result:**
- [ ] PASS
- [ ] FAIL

**Actual / Notes:**
> 

---

## B. Control Gating

> These tests validate the Allow Control switch. Turn it ON for the first time here.

---

### 9. Allow Control defaults OFF on first boot

**Steps:**
1. If not already tested in test 1, confirm `switch.abcdesp_allow_control` = OFF after a fresh flash

**Result:**
- [ ] PASS
- [ ] FAIL

**Actual / Notes:**
> 

---

### 10. Allow Control toggle enables writes

**Steps:**
1. Turn Allow Control ON
2. Change a setpoint from HA
3. Verify write succeeds (thermostat display updates)

**Expected:**
- Setpoint change appears on thermostat display
- Log shows write sent to bus

**Result:**
- [ ] PASS
- [ ] FAIL

**Actual / Notes:**
> 

---

### 11. Allow Control persists across reboot

**Steps:**
1. Turn Allow Control ON
2. Reboot ESP32 (HA → Developer Tools → Services → `esphome.abcdesp_restart`)
3. After reconnect, check Allow Control state

**Expected:**
- Allow Control restores to ON (persisted via ESPHome preferences)

**Result:**
- [ ] PASS
- [ ] FAIL

**Actual / Notes:**
> 

---

## C. Mode Changes

> Prerequisite: Allow Control = ON

---

### 12. Switch mode: Heat → Cool

**Steps:**
1. Set climate mode to Heat from HA (if not already)
2. Wait for thermostat display to confirm Heat mode
3. Switch climate mode to Cool from HA

**Expected:**
- Thermostat display shows Cool mode
- Climate entity mode updates to `cool`
- Action updates appropriately (idle or cooling depending on setpoint)

**Result:**
- [ ] PASS
- [ ] FAIL

**Actual / Notes:**
> 

---

### 13. Switch mode: Cool → Auto (Heat/Cool)

**Steps:**
1. With mode set to Cool, switch to Heat/Cool (Auto) from HA

**Expected:**
- Thermostat display shows Auto mode
- Climate entity mode updates to `heat_cool`
- Both heat and cool setpoints visible in HA

**Result:**
- [ ] PASS
- [ ] FAIL

**Actual / Notes:**
> 

---

### 14. Switch mode: Auto → Off

**Steps:**
1. With mode set to Auto, switch to Off from HA

**Expected:**
- Thermostat display shows system Off
- Climate entity mode updates to `off`
- Climate action → `off`
- Blower should stop (may take a few seconds for fan-off delay)

**Result:**
- [ ] PASS
- [ ] FAIL

**Actual / Notes:**
> 

---

### 15. Switch mode: Off → Heat

**Steps:**
1. With mode Off, switch to Heat from HA

**Expected:**
- Thermostat display shows Heat mode
- Climate entity updates to `heat`

**Result:**
- [ ] PASS
- [ ] FAIL

**Actual / Notes:**
> 

---

### 16. Mode change from thermostat reflects in HA

**Steps:**
1. Change mode on the physical thermostat (e.g., Heat → Cool)
2. Wait for a poll cycle (~5 seconds)

**Expected:**
- Climate entity in HA updates to match thermostat selection
- No writes sent by the ESP (read-only sync)

**Result:**
- [ ] PASS
- [ ] FAIL

**Actual / Notes:**
> 

---

## D. Fan Mode Changes

---

### 17. Cycle fan modes: Auto → Low → Med → High → Auto

**Steps (check each):**
- [ ] Set fan to Low → thermostat shows Low?
- [ ] Set fan to Medium → thermostat shows Med?
- [ ] Set fan to High → thermostat shows High?
- [ ] Set fan to Auto → thermostat shows Auto?

**Expected:**
- Climate entity `fan_mode` matches selection at each step
- If system is actively heating/cooling, blower speed changes audibly

**Result:**
- [ ] PASS
- [ ] FAIL

**Actual / Notes:**
> 

---

### 18. Fan mode change from thermostat reflects in HA

**Steps:**
1. Change fan mode on the physical thermostat
2. Wait for a poll cycle (~5 seconds)

**Expected:**
- Climate entity `fan_mode` in HA updates to match

**Result:**
- [ ] PASS
- [ ] FAIL

**Actual / Notes:**
> 

---

## E. Setpoint Changes

---

### 19. Change heat setpoint

**Steps:**
1. Set mode to Heat
2. Change target temperature from HA (e.g., 70°F)

**Expected:**
- Thermostat display shows new setpoint
- Climate entity `target_temperature` matches

**Setpoint sent:** ___°F &ensp; **Thermostat shows:** ___°F

**Result:**
- [ ] PASS
- [ ] FAIL

**Actual / Notes:**
> 

---

### 20. Change cool setpoint

**Steps:**
1. Set mode to Cool
2. Change target temperature from HA (e.g., 76°F)

**Expected:**
- Thermostat display shows new setpoint
- Climate entity `target_temperature` matches

**Setpoint sent:** ___°F &ensp; **Thermostat shows:** ___°F

**Result:**
- [ ] PASS
- [ ] FAIL

**Actual / Notes:**
> 

---

### 21. Change dual setpoints in Auto mode

**Steps:**
1. Set mode to Auto (Heat/Cool)
2. Change heat setpoint (target_temperature_low) to 68°F
3. Change cool setpoint (target_temperature_high) to 76°F

**Expected:**
- Thermostat display shows both setpoints
- Climate entity shows correct `target_temperature_low` and `target_temperature_high`

**Heat sent:** ___°F → **Thermostat:** ___°F &ensp; **Cool sent:** ___°F → **Thermostat:** ___°F

**Result:**
- [ ] PASS
- [ ] FAIL

**Actual / Notes:**
> 

---

### 22. Setpoint change from thermostat reflects in HA

**Steps:**
1. Change a setpoint on the physical thermostat
2. Wait for a poll cycle (~5 seconds)

**Expected:**
- Climate entity in HA updates to match the thermostat setpoint

**Result:**
- [ ] PASS
- [ ] FAIL

**Actual / Notes:**
> 

---

## F. Temperature Sensors

---

### 23. Current temperature reads correctly

**Steps:**
1. Compare thermostat display current temperature with the climate entity `current_temperature` in HA
2. If you have an independent thermometer, cross-check

**Expected:**
- Values match within ±1°F

**Thermostat:** ___°F &ensp; **HA:** ___°F &ensp; **Independent (if avail):** ___°F

**Result:**
- [ ] PASS
- [ ] FAIL

**Actual / Notes:**
> 

---

### 24. Outdoor temperature sensor

**Steps:**
1. Check `sensor.abcdesp_outdoor_temperature` in HA
2. Compare with the thermostat display (if shown) or weather forecast

**Expected:**
- Value is plausible for current conditions
- Updates periodically (within ~15 seconds of a real change)

**HA value:** ___°F &ensp; **Reference (tstat/weather):** ___°F

**Result:**
- [ ] PASS
- [ ] FAIL

**Actual / Notes:**
> 

---

### 25. Indoor humidity sensor

**Steps:**
1. Check `sensor.abcdesp_indoor_humidity` in HA
2. Compare with the thermostat display humidity (if shown)

**Expected:**
- Value is in 0–100% range, plausible for current indoor conditions

**HA value:** ___% &ensp; **Thermostat:** ___%

**Result:**
- [ ] PASS
- [ ] FAIL

**Actual / Notes:**
> 

---

### 26. HP coil temperature sensor

**Requires:** Heat pump on the RS-485 bus.

- [ ] **N/A** — no heat pump on bus (skip this test)

**Steps:**
1. Check `sensor.abcdesp_hp_coil_temperature` in HA
2. With system running in heat or cool, verify value is plausible:
   - Heating: coil temp should be warmer than outdoor
   - Cooling: coil temp should be colder than indoor

**Expected:**
- Value updates when system is actively running
- Returns to near-ambient when system is idle

**HA value (running):** ___°F &ensp; **HA value (idle):** ___°F

**Result:**
- [ ] PASS
- [ ] FAIL

**Actual / Notes:**
> 

---

### 27. Airflow CFM sensor

**Requires:** Air handler on the RS-485 bus.

- [ ] **N/A** — no air handler on bus (skip this test)

**Steps:**
1. Check `sensor.abcdesp_airflow_cfm` while system is idle → should be 0
2. Trigger heating or cooling; wait for blower to start
3. Verify CFM reads a plausible value (typically 400–2000 CFM)

**Expected:**
- 0 when blower is off
- Positive value when blower is running
- Higher values at higher fan speeds (Low < Med < High)

**CFM idle:** ___ &ensp; **CFM running:** ___ &ensp; **Fan speed:** ___

**Result:**
- [ ] PASS
- [ ] FAIL

**Actual / Notes:**
> 

---

## G. Stage Sensors

---

### 28. Heat stage sensor

**Requires:** Air handler on the RS-485 bus, system in Heat mode.

- [ ] **N/A** — no air handler on bus (skip this test)

**Steps:**
1. Set mode to Heat with a setpoint well above current temperature
2. Wait for system to call for heat

**Expected:**
- `sensor.abcdesp_heat_stage` updates from 0 to 1 (or higher for multi-stage)
- `text_sensor.abcdesp_heat_stage_label` updates: "Off" → "Low" (→ "Med" → "High")
- When setpoint is satisfied and system stops, stage returns to 0 / "Off"

**Stage observed:** ___ &ensp; **Label observed:** _______________

**Result:**
- [ ] PASS
- [ ] FAIL

**Actual / Notes:**
> 

---

### 29. HP stage sensor

**Requires:** Heat pump on the RS-485 bus, system in Heat or Cool mode.

- [ ] **N/A** — no heat pump on bus (skip this test)

**Steps:**
1. Trigger a heating or cooling call
2. Wait for heat pump to start

**Expected:**
- `sensor.abcdesp_hp_stage` updates from 0 to 1 (or 2 for high stage)
- `text_sensor.abcdesp_hp_stage_label` updates: "Off" → "Low" (→ "High")
- Stage returns to 0 / "Off" when call ends

**Stage observed:** ___ &ensp; **Label observed:** _______________

**Result:**
- [ ] PASS
- [ ] FAIL

**Actual / Notes:**
> 

---

## H. Binary Sensors

---

### 30. Blower running sensor

**Steps:**
1. With system idle, verify `binary_sensor.abcdesp_blower_running` = OFF
2. Trigger a heating or cooling call
3. Wait for blower to start (there may be a startup delay)

**Expected:**
- Sensor flips to ON when blower engages
- Flips back to OFF when blower stops (may lag by fan-off delay, typically 60–90 seconds)

**Result:**
- [ ] PASS
- [ ] FAIL

**Actual / Notes:**
> 

---

### 31. Hold Active sensor

**Steps:**
1. Clear any holds; verify `binary_sensor.abcdesp_hold_active` = OFF
2. Change a setpoint from HA (this should activate a hold)
3. Verify sensor = ON
4. Press "Clear Hold" button
5. Verify sensor = OFF

**Expected:**
- State transitions match hold activation and clearing
- Also changes if hold is set or cleared from the physical thermostat

**Result:**
- [ ] PASS
- [ ] FAIL

**Actual / Notes:**
> 

---

### 32. Communication OK sensor

**Steps:**
1. With system running normally, verify `binary_sensor.abcdesp_communication_ok` = ON
2. (Optional/destructive) Disconnect the RS-485 data line briefly
3. Wait >30 seconds

**Expected:**
- Sensor = ON during normal operation
- Sensor → OFF if no response received within 30 seconds
- Sensor → ON again when communication resumes

- [ ] **Skipped** disconnect test (destructive)

**Result:**
- [ ] PASS
- [ ] FAIL

**Actual / Notes:**
> 

---

## I. Climate Action State

---

### 33. Action reflects current system activity

**Steps (check each transition):**
- [ ] Set mode to Off → action = `off`
- [ ] Set mode to Heat, setpoint well above current temp → action = `heating`
- [ ] Observe blower running after heat call stops → action = `fan`
- [ ] After blower stops → action = `idle`
- [ ] Set mode to Cool, setpoint well below current temp → action = `cooling`

**Expected:**
- Action state correctly tracks: off, heating, cooling, fan, idle
- Transitions happen within one poll cycle (~5 seconds)

**Result:**
- [ ] PASS
- [ ] FAIL

**Actual / Notes:**
> 

---

## J. Communication Resilience

---

### 34. Recovery after brief bus interruption

- [ ] **Skipped** (destructive — requires disconnecting RS-485)

**Steps:**
1. Briefly disconnect then reconnect the RS-485 data lines (< 5 seconds)
2. Watch ESPHome logs and Comms OK sensor

**Expected:**
- Comms OK may briefly flicker but should not drop (30-second timeout)
- Polling resumes normally after reconnection
- No error codes on thermostat

**Result:**
- [ ] PASS
- [ ] FAIL

**Actual / Notes:**
> 

---

### 35. CRC failure logging

**Steps:**
1. Monitor ESPHome logs at DEBUG level during normal operation for ~5 minutes
2. Check for any CRC failure messages

**Expected:**
- Occasional CRC failures are normal on a noisy bus (logged at DEBUG)
- After 10 consecutive CRC failures, an ERROR-level log appears
- System continues operating despite CRC failures (frames are just discarded)

**CRC failures observed:** ___ in ___ minutes

**Result:**
- [ ] PASS
- [ ] FAIL

**Actual / Notes:**
> 

---

## Summary

| Section | Tests | Passed | Failed | Skipped |
|---------|-------|--------|--------|---------|
| A. Read-Only Mode | 1–8 | | | |
| B. Control Gating | 9–11 | | | |
| C. Mode Changes | 12–16 | | | |
| D. Fan Mode Changes | 17–18 | | | |
| E. Setpoint Changes | 19–22 | | | |
| F. Temperature Sensors | 23–27 | | | |
| G. Stage Sensors | 28–29 | | | |
| H. Binary Sensors | 30–32 | | | |
| I. Climate Action | 33 | | | |
| J. Communication | 34–35 | | | |
| **TOTAL** | **35** | | | |

## Failure Criteria

If any of the following occur, **stop testing and investigate**:
- Thermostat shows error codes or enters fault state
- HVAC system starts/stops unexpectedly after a command
- Sustained NAK responses to writes (check logs)
- Communication lost and does not recover after 60 seconds
- Thermostat display persistently disagrees with HA entity state
- Blower runs continuously with no heating/cooling call
