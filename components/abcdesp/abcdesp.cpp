#include "abcdesp.h"
#include "esphome/core/log.h"
#include <cmath>
#include <cstring>

namespace esphome {
namespace abcdesp {

static const char *const TAG = "abcdesp";

// ==========================================================================
// ClearHoldButton — press_action calls parent clear_hold()
// ==========================================================================
void ClearHoldButton::press_action() {
  if (parent_ != nullptr) {
    parent_->clear_hold();
  }
}

// ==========================================================================
// HoldDurationNumber — update default hold duration at runtime
// ==========================================================================
void HoldDurationNumber::control(float value) {
  publish_state(value);
  float v = value;
  this->pref_.save(&v);
  if (parent_ != nullptr) {
    parent_->set_hold_duration_minutes(static_cast<uint16_t>(value));
  }
}

void HoldDurationNumber::setup_restore() {
  this->pref_ = global_preferences->make_preference<float>(this->get_object_id_hash());
  float restored;
  if (this->pref_.load(&restored)) {
    this->publish_state(restored);
    if (parent_ != nullptr) {
      parent_->set_hold_duration_minutes(static_cast<uint16_t>(restored));
    }
  }
}

// ==========================================================================
// SetHoldTimeNumber — adjust remaining time on an active hold
// ==========================================================================
void SetHoldTimeNumber::control(float value) {
  if (parent_ != nullptr) {
    parent_->adjust_hold(static_cast<uint16_t>(value));
  }
}

// ==========================================================================
// Vacation parameter numbers — store value for next vacation activation
// ==========================================================================
void VacationDaysNumber::control(float value) {
  if (parent_ != nullptr && !parent_->is_control_allowed()) {
    ESP_LOGW(TAG, "Vacation Days change blocked: Allow Control is locked");
    return;
  }
  publish_state(value);
}

void VacationMinTempNumber::control(float value) {
  if (parent_ != nullptr && !parent_->is_control_allowed()) {
    ESP_LOGW(TAG, "Vacation Min Temp change blocked: Allow Control is locked");
    return;
  }
  publish_state(value);
}

void VacationMaxTempNumber::control(float value) {
  if (parent_ != nullptr && !parent_->is_control_allowed()) {
    ESP_LOGW(TAG, "Vacation Max Temp change blocked: Allow Control is locked");
    return;
  }
  publish_state(value);
}

// ==========================================================================
// Temperature unit conversion helpers
// ==========================================================================
static float f_to_c(float f) { return (f - 32.0f) * 5.0f / 9.0f; }
static float c_to_f(float c) { return c * 9.0f / 5.0f + 32.0f; }

// ==========================================================================
// CRC-16/ARC (poly 0x8005 reflected = 0xA001)
// ==========================================================================
uint16_t AbcdEspComponent::crc16(const uint8_t *data, uint16_t len) {
  uint16_t crc = 0x0000;
  for (uint16_t i = 0; i < len; i++) {
    crc ^= data[i];
    for (uint8_t j = 0; j < 8; j++) {
      if (crc & 0x0001) {
        crc = (crc >> 1) ^ CRC_POLY;
      } else {
        crc >>= 1;
      }
    }
  }
  return crc;
}

// ==========================================================================
// Frame parsing — validate CRC, extract fields
// ==========================================================================
bool AbcdEspComponent::parse_frame(const uint8_t *buf, uint16_t len,
                                           InfinityFrame &frame) {
  if (len < FRAME_MIN_LEN) {
    return false;
  }

  uint8_t data_len = buf[4];
  uint16_t expected_len = FRAME_HEADER_LEN + data_len + FRAME_CRC_LEN;
  if (len < expected_len) {
    return false;
  }

  // CRC check — CRC is little-endian after header+data
  uint16_t calc = crc16(buf, FRAME_HEADER_LEN + data_len);
  uint16_t received = buf[FRAME_HEADER_LEN + data_len] |
                      (buf[FRAME_HEADER_LEN + data_len + 1] << 8);
  if (calc != received) {
    return false;
  }

  frame.dst = (buf[0] << 8) | buf[1];
  frame.src = (buf[2] << 8) | buf[3];
  frame.length = data_len;
  frame.func = buf[7];
  if (data_len > 0) {
    memcpy(frame.data, buf + FRAME_HEADER_LEN, data_len);
  }

  return true;
}

// ==========================================================================
// Frame building — header + data + CRC
// ==========================================================================
void AbcdEspComponent::build_frame(const InfinityFrame &frame,
                                           uint8_t *out, uint16_t &out_len) {
  out[0] = (frame.dst >> 8) & 0xFF;
  out[1] = frame.dst & 0xFF;
  out[2] = (frame.src >> 8) & 0xFF;
  out[3] = frame.src & 0xFF;
  out[4] = frame.length;
  out[5] = 0x00;  // PID
  out[6] = 0x00;  // EXT
  out[7] = frame.func;
  if (frame.length > 0) {
    memcpy(out + FRAME_HEADER_LEN, frame.data, frame.length);
  }

  uint16_t payload_end = FRAME_HEADER_LEN + frame.length;
  uint16_t crc = crc16(out, payload_end);
  out[payload_end] = crc & 0xFF;         // low byte first
  out[payload_end + 1] = (crc >> 8) & 0xFF;
  out_len = payload_end + FRAME_CRC_LEN;
}

// ==========================================================================
// Send frame over UART with flow control
// ==========================================================================
void AbcdEspComponent::send_frame(const uint8_t *buf, uint16_t len) {
  if (flow_pin_ != nullptr) {
    flow_pin_->digital_write(true);   // assert TX
  }
  delayMicroseconds(TX_SETTLE_US);

  write_array(buf, len);
  flush();

  delayMicroseconds(TX_SETTLE_US);
  if (flow_pin_ != nullptr) {
    flow_pin_->digital_write(false);  // back to RX
  }

  last_send_ms_ = millis();
}

// ==========================================================================
// Send ACK06 with data=0x00 (write acknowledgment)
// ==========================================================================
void AbcdEspComponent::send_ack(uint16_t dst) {
  InfinityFrame ack;
  ack.dst = dst;
  ack.src = ADDR_SAM;
  ack.func = FUNC_ACK06;
  ack.length = 1;
  ack.data[0] = 0x00;

  uint8_t buf[16];
  uint16_t len;
  build_frame(ack, buf, len);
  send_frame(buf, len);

  ESP_LOGD(TAG, "Sent ACK to 0x%04X", dst);
}

// ==========================================================================
// Send a READ request
// ==========================================================================
void AbcdEspComponent::send_read_request(uint16_t dst, uint8_t table,
                                                  uint8_t row) {
  // Enforce minimum gap between self-initiated frames
  uint32_t now = millis();
  if (now - last_send_ms_ < MIN_FRAME_GAP_MS) {
    return;
  }

  InfinityFrame f;
  f.dst = dst;
  f.src = ADDR_SAM;
  f.func = FUNC_READ;
  f.length = 3;
  f.data[0] = REG_PREFIX;
  f.data[1] = table;
  f.data[2] = row;

  uint8_t buf[16];
  uint16_t len;
  build_frame(f, buf, len);
  send_frame(buf, len);

  awaiting_response_ = true;
  request_sent_ms_ = millis();
  pending_table_ = table;
  pending_row_ = row;
  pending_is_write_ = false;

  ESP_LOGD(TAG, "Sent READ 0x%02X%02X to 0x%04X", table, row, dst);
}

// ==========================================================================
// Send a WRITE request
// ==========================================================================
void AbcdEspComponent::send_write_request(uint16_t dst, uint8_t table,
                                                   uint8_t row,
                                                   const uint8_t *payload,
                                                   uint8_t payload_len) {
  uint32_t now = millis();
  if (now - last_send_ms_ < MIN_FRAME_GAP_MS) {
    ESP_LOGW(TAG, "WRITE 0x%02X%02X dropped — frame gap not satisfied (try again next loop)",
             table, row);
    return;
  }

  if (payload_len > 252) {
    ESP_LOGE(TAG, "WRITE 0x%02X%02X payload too large (%d bytes, max 252) — dropped",
             table, row, payload_len);
    return;
  }

  InfinityFrame f;
  f.dst = dst;
  f.src = ADDR_SAM;
  f.func = FUNC_WRITE;
  f.length = 3 + payload_len;
  f.data[0] = REG_PREFIX;
  f.data[1] = table;
  f.data[2] = row;
  if (payload_len > 0) {
    memcpy(f.data + 3, payload, payload_len);
  }

  uint8_t buf[270];
  uint16_t len;
  build_frame(f, buf, len);
  send_frame(buf, len);

  ESP_LOGD(TAG, "Sent WRITE 0x%02X%02X to 0x%04X (%d bytes)", table, row, dst,
           payload_len);

  // Track the write so NAK logging identifies the correct request
  pending_table_ = table;
  pending_row_ = row;
  pending_is_write_ = true;
}

// ==========================================================================
// Component setup
// ==========================================================================
void AbcdEspComponent::setup() {
  ESP_LOGI(TAG, "ABCDESP component initializing");
  if (flow_pin_ != nullptr) {
    flow_pin_->setup();
    flow_pin_->digital_write(false);  // start in RX mode
  }
  // Initialize Allow Control lock to LOCKED (control blocked until user unlocks)
  if (allow_control_lock_ != nullptr) {
    allow_control_lock_->publish_state(lock::LOCK_STATE_LOCKED);
  }
  // Restore hold duration number from flash, or initialize from compile-time config
  if (hold_duration_number_ != nullptr) {
    hold_duration_number_->setup_restore();
    if (std::isnan(hold_duration_number_->state)) {
      hold_duration_number_->publish_state(static_cast<float>(hold_duration_minutes_));
    }
  }
  // Initialize vacation number entities with defaults
  if (vacation_days_number_ != nullptr && std::isnan(vacation_days_number_->state)) {
    vacation_days_number_->publish_state(7.0f);
  }
  if (vacation_min_temp_number_ != nullptr && std::isnan(vacation_min_temp_number_->state)) {
    vacation_min_temp_number_->publish_state(60.0f);
  }
  if (vacation_max_temp_number_ != nullptr && std::isnan(vacation_max_temp_number_->state)) {
    vacation_max_temp_number_->publish_state(80.0f);
  }
  rx_len_ = 0;
  last_poll_ms_ = millis();
  poll_step_ = 0;
}

// ==========================================================================
// Component loop — drain UART, parse frames, poll
// ==========================================================================
void AbcdEspComponent::loop() {
  // --- Drain UART into ring buffer ---
  while (available() > 0 && rx_len_ < RX_BUF_SIZE) {
    uint8_t byte;
    if (read_byte(&byte)) {
      rx_buf_[rx_len_++] = byte;
    }
  }

  // --- Try to parse frames from buffer ---
  while (rx_len_ >= FRAME_MIN_LEN) {
    uint8_t data_len = rx_buf_[4];
    uint16_t frame_len = FRAME_HEADER_LEN + data_len + FRAME_CRC_LEN;

    if (frame_len > RX_BUF_SIZE) {
      // Corrupt length, skip one byte
      memmove(rx_buf_, rx_buf_ + 1, --rx_len_);
      continue;
    }

    if (rx_len_ < frame_len) {
      break;  // need more bytes
    }

    InfinityFrame frame;
    if (parse_frame(rx_buf_, frame_len, frame)) {
      if (crc_fail_count_ > 0) {
        ESP_LOGD(TAG, "CRC re-synced after %u failures", crc_fail_count_);
        crc_fail_count_ = 0;
      }
      handle_frame(frame);
      // Consume the frame
      rx_len_ -= frame_len;
      if (rx_len_ > 0) {
        memmove(rx_buf_, rx_buf_ + frame_len, rx_len_);
      }
    } else {
      // CRC failed — skip one byte, try to re-sync
      crc_fail_count_++;
      if (crc_fail_count_ == CRC_FAIL_LOG_THRESHOLD) {
        ESP_LOGE(TAG, "Repeated CRC failures (%u consecutive) — check RS-485 wiring",
                 crc_fail_count_);
      }
      memmove(rx_buf_, rx_buf_ + 1, --rx_len_);
    }
  }

  // --- Check for response timeout ---
  if (awaiting_response_ && (millis() - request_sent_ms_ > RESPONSE_TIMEOUT_MS)) {
    ESP_LOGW(TAG, "Response timeout for READ 0x%02X%02X", pending_table_,
             pending_row_);
    awaiting_response_ = false;
  }

  // --- Check communication health ---
  if (comms_ok_ && last_successful_response_ms_ > 0 &&
      (millis() - last_successful_response_ms_ > COMMS_TIMEOUT_MS)) {
    comms_ok_ = false;
    ESP_LOGE(TAG, "Communication lost — no response in %d ms", COMMS_TIMEOUT_MS);
    if (comms_ok_sensor_ != nullptr) {
      comms_ok_sensor_->publish_state(false);
    }
  }

  // --- Log bus device detection summary after startup ---
  if (!bus_detection_logged_ && millis() > 60000) {
    bus_detection_logged_ = true;
    ESP_LOGI(TAG, "Bus device summary: thermostat=%s  air_handler=%s  heat_pump=%s",
             seen_thermostat_ ? "yes" : "NO",
             seen_air_handler_ ? "yes" : "NO",
             seen_heat_pump_ ? "yes" : "NO");
    if (!seen_thermostat_) {
      ESP_LOGW(TAG, "No thermostat detected — check RS-485 wiring and baud rate");
    }
    if (!seen_air_handler_) {
      ESP_LOGW(TAG, "No air handler detected — airflow/blower/heat stage sensors will not update");
    }
    if (!seen_heat_pump_) {
      ESP_LOGW(TAG, "No heat pump detected — HP sensors will not update (normal if system has no heat pump)");
    }
  }

  // --- Periodic polling of thermostat ---
  uint32_t now = millis();
  if (!awaiting_response_ && !write_pending_ &&
      (now - last_poll_ms_ >= POLL_INTERVAL_MS)) {
    poll_thermostat();
    last_poll_ms_ = now;

    // Update last-seen sensor every poll cycle
    if (last_seen_sensor_ != nullptr && last_successful_response_ms_ > 0) {
      last_seen_sensor_->publish_state(
          static_cast<float>(now - last_successful_response_ms_) / 1000.0f);
    }
  }

  // --- Auto-clear temporary hold ---
  {
    uint16_t dur = (hold_duration_number_ != nullptr)
                       ? static_cast<uint16_t>(hold_duration_number_->state)
                       : hold_duration_minutes_;
    if (dur > 0 && hold_set_ms_ > 0 &&
        (now - hold_set_ms_ >= static_cast<uint32_t>(dur) * 60000U)) {
      ESP_LOGI(TAG, "Temporary hold expired after %d minutes — clearing", dur);
      hold_set_ms_ = 0;
      clear_hold();
    }
  }

  // --- Send pending write if gap is satisfied ---
  if (write_pending_ && !awaiting_response_ &&
      (now - last_send_ms_ >= MIN_FRAME_GAP_MS)) {
    // Re-check Allow Control in case it was toggled off after queuing
    if (allow_control_lock_ == nullptr || !is_control_allowed()) {
      ESP_LOGW(TAG, "Pending write discarded — Allow Control locked after queuing");
      write_pending_ = false;
    } else {
      send_write_request(ADDR_TSTAT, TBL_SAM_INFO, ROW_SAM_ZONES,
                         write_buf_, write_len_);
      write_pending_ = false;
    }
  }
}

// ==========================================================================
// Poll thermostat — cycle through 3B02, 3B03, 3B04
// ==========================================================================
void AbcdEspComponent::poll_thermostat() {
  switch (poll_step_) {
    case 0:
      send_read_request(ADDR_TSTAT, TBL_SAM_INFO, ROW_SAM_STATE);
      break;
    case 1:
      send_read_request(ADDR_TSTAT, TBL_SAM_INFO, ROW_SAM_ZONES);
      break;
    case 2:
      send_read_request(ADDR_TSTAT, TBL_SAM_INFO, ROW_SAM_VACATION);
      break;
  }
  poll_step_ = (poll_step_ + 1) % 3;
}

// ==========================================================================
// Frame dispatch
// ==========================================================================
void AbcdEspComponent::handle_frame(const InfinityFrame &frame) {
  ESP_LOGD(TAG, "Frame: 0x%04X -> 0x%04X func=0x%02X len=%d",
           frame.src, frame.dst, frame.func, frame.length);

  // --- ACK06/Response to our READ request ---
  if (frame.func == FUNC_ACK06 && frame.dst == ADDR_SAM) {
    handle_ack_response(frame);
    return;
  }

  // --- NAK to our request ---
  if (frame.func == FUNC_NAK && frame.dst == ADDR_SAM) {
    ESP_LOGE(TAG, "NAK received from 0x%04X for pending %s 0x%02X%02X — command rejected by thermostat",
             frame.src, pending_is_write_ ? "WRITE" : "READ",
             pending_table_, pending_row_);
    awaiting_response_ = false;
    return;
  }

  // --- WRITE from thermostat to SAM (3B0E ack) ---
  if (frame.func == FUNC_WRITE && frame.dst == ADDR_SAM &&
      frame.src == ADDR_TSTAT) {
    handle_write_to_sam(frame);
    return;
  }

  // --- READ directed at SAM — we don't serve registers, ignore ---
  if (frame.func == FUNC_READ && frame.dst == ADDR_SAM) {
    // Thermostat polls SAM for 030D, 0104, etc. — just ignore.
    // The thermostat will time out, which is normal behavior.
    return;
  }

  // --- Snoop: ACK06 responses from air handler or heat pump ---
  if (frame.func == FUNC_ACK06 && frame.length > 3) {
    uint16_t src_class = frame.src & 0xFF00;
    if (src_class == 0x4000 || src_class == 0x4200) {
      if (!seen_air_handler_) {
        seen_air_handler_ = true;
        ESP_LOGI(TAG, "Detected air handler on bus (0x%04X)", frame.src);
      }
      handle_snooped_response(frame);
    } else if (src_class == 0x5000 || src_class == 0x5100 || src_class == 0x5200) {
      if (!seen_heat_pump_) {
        seen_heat_pump_ = true;
        ESP_LOGI(TAG, "Detected heat pump on bus (0x%04X)", frame.src);
      }
      handle_snooped_response(frame);
    }
  }
}

// ==========================================================================
// Handle ACK06 response to our READ
// ==========================================================================
void AbcdEspComponent::handle_ack_response(const InfinityFrame &frame) {
  awaiting_response_ = false;
  last_successful_response_ms_ = millis();

  // Update comms health
  if (!comms_ok_) {
    comms_ok_ = true;
    if (comms_ok_sensor_ != nullptr) {
      comms_ok_sensor_->publish_state(true);
    }
  }

  // Track thermostat detection
  if (!seen_thermostat_) {
    seen_thermostat_ = true;
    ESP_LOGI(TAG, "Detected thermostat on bus (0x%04X)", frame.src);
  }

  if (frame.length <= 3) {
    return;  // Simple write-ack (data=0x00), nothing to parse
  }

  // Data layout: [0]=0x00, [1]=table, [2]=row, [3..]=register content
  uint8_t table = frame.data[1];
  uint8_t row = frame.data[2];
  const uint8_t *reg_data = frame.data + 3;
  uint8_t reg_len = frame.length - 3;

  if (table == TBL_SAM_INFO && row == ROW_SAM_STATE) {
    parse_tstat_state(reg_data, reg_len);
  } else if (table == TBL_SAM_INFO && row == ROW_SAM_ZONES) {
    parse_tstat_zones(reg_data, reg_len);
  } else if (table == TBL_SAM_INFO && row == ROW_SAM_VACATION) {
    parse_vacation(reg_data, reg_len);
  }
}

// ==========================================================================
// Handle WRITE from thermostat to SAM — just ACK it
// ==========================================================================
void AbcdEspComponent::handle_write_to_sam(const InfinityFrame &frame) {
  // ACK any writes from thermostat (typically 3B0E acknowledgments)
  send_ack(frame.src);
}

// ==========================================================================
// Handle snooped ACK06 responses from air handler / heat pump
// ==========================================================================
void AbcdEspComponent::handle_snooped_response(
    const InfinityFrame &frame) {
  if (frame.length <= 3) {
    return;
  }

  uint8_t table = frame.data[1];
  uint8_t row = frame.data[2];
  const uint8_t *reg_data = frame.data + 3;
  uint8_t reg_len = frame.length - 3;

  uint16_t src_class = frame.src & 0xFF00;

  // Air handler responses (0x40xx, 0x42xx)
  if (src_class == 0x4000 || src_class == 0x4200) {
    if (table == TBL_RLCSMAIN && row == ROW_AH_06) {
      parse_airhandler_06(reg_data, reg_len);
    } else if (table == TBL_RLCSMAIN && row == ROW_AH_16) {
      parse_airhandler_16(reg_data, reg_len);
    }
  }

  // Heat pump responses (0x50xx, 0x51xx, 0x52xx)
  if (src_class == 0x5000 || src_class == 0x5100 || src_class == 0x5200) {
    if (table == TBL_HEATPUMP && row == ROW_HP_01) {
      parse_heatpump_01(reg_data, reg_len);
    } else if (table == TBL_HEATPUMP && row == ROW_HP_02) {
      parse_heatpump_02(reg_data, reg_len);
    }
  }
}

// ==========================================================================
// Parse 3B02 — thermostat current state (29 bytes with 3-byte header)
// Offsets relative to register content start (after 3-byte register address):
//   [0]     = zone bitmap
//   [1-2]   = field flags (uint16)
//   [3-10]  = zone 1-8 current temps (uint8, °F)
//   [11-18] = zone 1-8 current humidity (uint8, %RH)
//   [19]    = unknown
//   [20]    = outdoor air temp (int8, °F)
//   [21]    = unoccupied zone flags
//   [22]    = mode (low nibble: 0=heat,1=cool,2=auto,5=off; bits 5-7=stage)
//   [23-28] = misc
// ==========================================================================
void AbcdEspComponent::parse_tstat_state(const uint8_t *data,
                                                  uint8_t len) {
  if (len < 29) {
    ESP_LOGW(TAG, "3B02 too short: %d bytes", len);
    return;
  }

  // Zone 1 temp — uint8, direct °F
  indoor_temp_ = static_cast<float>(data[3]);
  indoor_humidity_ = data[11];
  outdoor_temp_ = static_cast<float>(static_cast<int8_t>(data[20]));
  current_mode_ = data[22] & 0x0F;

  ESP_LOGD(TAG, "3B02: indoor=%.0f°F  humid=%d%%  outdoor=%.0f°F  mode=%d",
           indoor_temp_, indoor_humidity_, outdoor_temp_, current_mode_);

  publish_climate_state();
  publish_sensors();
}

// ==========================================================================
// Parse 3B03 — thermostat zone settings (150 bytes with 3-byte header)
// Offsets relative to register content:
//   [0]     = zone bitmap
//   [1-2]   = field flags
//   [3-10]  = fan mode for zones 1-8 (uint8)
//   [11]    = hold flag bitmap
//   [12-19] = heat setpoints for zones 1-8 (uint8, °F)
//   [20-27] = cool setpoints for zones 1-8 (uint8, °F)
//   [28-35] = target humidity for zones 1-8 (uint8, %RH)
//   [36]    = fan auto config
//   [37]    = timed override flag bitmap (bit per zone)
//   [38-53] = override time remaining (uint16 BE per zone, minutes)
//   [54-149]= zone names (12 bytes each)
// ==========================================================================
void AbcdEspComponent::parse_tstat_zones(const uint8_t *data,
                                                  uint8_t len) {
  if (len < 28) {
    ESP_LOGW(TAG, "3B03 too short: %d bytes", len);
    return;
  }

  fan_mode_ = data[3];        // Zone 1 fan mode
  zone_hold_ = data[11];
  heat_setpoint_ = data[12];  // Zone 1 heat setpoint
  cool_setpoint_ = data[20];  // Zone 1 cool setpoint

  // Parse timed override fields if present (bytes 37-53)
  if (len >= 40) {
    zone_override_flag_ = data[37];
    zone1_override_minutes_ = (data[38] << 8) | data[39];
  } else {
    zone_override_flag_ = 0;
    zone1_override_minutes_ = 0;
  }

  ESP_LOGD(TAG, "3B03: fan=%d  hold=0x%02X  heat=%d  cool=%d  override=0x%02X  mins=%d",
           fan_mode_, zone_hold_, heat_setpoint_, cool_setpoint_,
           zone_override_flag_, zone1_override_minutes_);

  // Publish hold status
  bool hold_active = (zone_hold_ & 0x01) != 0;
  if (hold_active_sensor_ != nullptr &&
      (!hold_active_initialized_ || hold_active != prev_hold_active_)) {
    hold_active_sensor_->publish_state(hold_active);
    prev_hold_active_ = hold_active;
    hold_active_initialized_ = true;
  }

  // Publish hold time remaining (0 when not on timed override)
  uint16_t remaining = (zone_override_flag_ & 0x01) ? zone1_override_minutes_ : 0;
  if (hold_time_remaining_sensor_ != nullptr &&
      remaining != prev_override_minutes_) {
    hold_time_remaining_sensor_->publish_state(remaining);
    prev_override_minutes_ = remaining;
  }

  publish_climate_state();
}

// ==========================================================================
// Parse 0306 — air handler blower RPM
// Per Infinitive api.go: data[1:5] = blower RPM (uint16 at offset 1)
// But the wiki says: [0]=unknown, [1-2]=blower_rpm, [3-4]=indoor_airflow_cfm
// ==========================================================================
void AbcdEspComponent::parse_airhandler_06(const uint8_t *data,
                                                    uint8_t len) {
  if (len < 5) {
    return;
  }

  blower_rpm_ = (data[1] << 8) | data[2];
  // 0306 also has airflow but 0316 is more frequently updated
  bool was_running = blower_running_;
  blower_running_ = (blower_rpm_ > 0);

  ESP_LOGD(TAG, "0306: blower RPM=%d", blower_rpm_);

  if (blower_running_ != was_running) {
    publish_sensors();
  }
}

// ==========================================================================
// Parse 0316 — air handler CFM and heat state
// Per wiki: [0]=state (00=no_heat,01=low,02=med,03=high),
//           [1-3]=unknown, [4-5]=airflow_cfm
// Per Infinitive api.go: data[4:8]=CFM, data[0]&0x03=elecHeat
// ==========================================================================
void AbcdEspComponent::parse_airhandler_16(const uint8_t *data,
                                                    uint8_t len) {
  if (len < 6) {
    return;
  }

  heat_stage_ = data[0];
  airflow_cfm_ = (data[4] << 8) | data[5];
  blower_running_ = (airflow_cfm_ > 0 || blower_rpm_ > 0);

  ESP_LOGD(TAG, "0316: heat_stage=%d  CFM=%d", heat_stage_, airflow_cfm_);

  publish_sensors();
}

// ==========================================================================
// Parse 3E01 — heat pump outside/coil temps (int16/16 format)
// Per Infinitive api.go:
//   data[0:2] = outside_temp * 16  (uint16 big-endian)
//   data[2:4] = coil_temp * 16
// ==========================================================================
void AbcdEspComponent::parse_heatpump_01(const uint8_t *data,
                                                  uint8_t len) {
  if (len < 4) {
    return;
  }

  uint16_t raw_outside = (data[0] << 8) | data[1];
  uint16_t raw_coil = (data[2] << 8) | data[3];
  hp_outside_temp_ = static_cast<float>(raw_outside) / 16.0f;
  hp_coil_temp_ = static_cast<float>(raw_coil) / 16.0f;

  // Log raw values so users can verify encoding in cold weather.
  // If raw_outside > 0x7FFF, the value may be a signed negative temperature
  // that we are misinterpreting as unsigned. See GitHub issue notes.
  if (raw_outside > 0x7FFF || raw_coil > 0x7FFF) {
    ESP_LOGW(TAG, "3E01 raw values may be signed-negative: outside=0x%04X (%.1f°F)  coil=0x%04X (%.1f°F)",
             raw_outside, hp_outside_temp_, raw_coil, hp_coil_temp_);
  }

  ESP_LOGD(TAG, "3E01: outside=%.1f°F  coil=%.1f°F  (raw: 0x%04X, 0x%04X)",
           hp_outside_temp_, hp_coil_temp_, raw_outside, raw_coil);

  // Use heat pump outside temp if we have it (higher precision than 3B02)
  if (!std::isnan(hp_outside_temp_)) {
    outdoor_temp_ = hp_outside_temp_;
    publish_sensors();
  }
}

// ==========================================================================
// Parse 3E02 — heat pump stage
// Per Infinitive api.go: data[0] >> 1 = stage
// ==========================================================================
void AbcdEspComponent::parse_heatpump_02(const uint8_t *data,
                                                  uint8_t len) {
  if (len < 1) {
    return;
  }
  hp_stage_ = data[0] >> 1;
  ESP_LOGD(TAG, "3E02: HP stage=%d", hp_stage_);
  publish_sensors();
}

// ==========================================================================
// Parse 3B04 — vacation settings (11 bytes)
// Layout: [0]=vacation_active, [1-2]=days*7(BE), [3]=mintemp, [4]=maxtemp,
//         [5]=minhumidity, [6]=maxhumidity, [7]=fanmode
// ==========================================================================
void AbcdEspComponent::parse_vacation(const uint8_t *data, uint8_t len) {
  if (len < 1) {
    return;
  }

  bool was_active = vacation_active_;
  vacation_active_ = (data[0] != 0);

  // Parse vacation parameters if present (bytes 1-7)
  if (len >= 8) {
    uint16_t days_x7 = (data[1] << 8) | data[2];
    uint8_t vac_days = (days_x7 > 0) ? (days_x7 / 7) : 0;
    uint8_t vac_min_temp = data[3];
    uint8_t vac_max_temp = data[4];

    ESP_LOGD(TAG, "3B04: vacation=%s  days=%d  min=%d°F  max=%d°F",
             vacation_active_ ? "active" : "off", vac_days, vac_min_temp, vac_max_temp);

    // Update number entities with current thermostat values when vacation is active
    if (vacation_active_) {
      if (vacation_days_number_ != nullptr) {
        vacation_days_number_->publish_state(static_cast<float>(vac_days));
      }
      if (vacation_min_temp_number_ != nullptr) {
        vacation_min_temp_number_->publish_state(static_cast<float>(vac_min_temp));
      }
      if (vacation_max_temp_number_ != nullptr) {
        vacation_max_temp_number_->publish_state(static_cast<float>(vac_max_temp));
      }
    }
  } else {
    ESP_LOGD(TAG, "3B04: vacation=%s", vacation_active_ ? "active" : "off");
  }

  if (!vacation_initialized_ || vacation_active_ != was_active) {
    vacation_initialized_ = true;
    publish_climate_state();
  }
}

// ==========================================================================
// Climate traits
// ==========================================================================
climate::ClimateTraits AbcdEspComponent::traits() {
  auto traits = climate::ClimateTraits();
  traits.add_feature_flags(climate::CLIMATE_SUPPORTS_CURRENT_TEMPERATURE |
                           climate::CLIMATE_SUPPORTS_TWO_POINT_TARGET_TEMPERATURE);
  traits.set_visual_min_temperature(f_to_c(40));   // 40°F ≈ 4.4°C
  traits.set_visual_max_temperature(f_to_c(99));   // 99°F ≈ 37.2°C
  traits.set_visual_target_temperature_step(0.5);   // 0.5°C step, rounds to nearest °F
  traits.set_visual_current_temperature_step(0.1);  // 0.1°C for display precision

  traits.set_supported_modes({
      climate::CLIMATE_MODE_OFF,
      climate::CLIMATE_MODE_HEAT,
      climate::CLIMATE_MODE_COOL,
      climate::CLIMATE_MODE_HEAT_COOL,
  });

  traits.set_supported_fan_modes({
      climate::CLIMATE_FAN_AUTO,
      climate::CLIMATE_FAN_LOW,
      climate::CLIMATE_FAN_MEDIUM,
      climate::CLIMATE_FAN_HIGH,
  });

  traits.set_supported_presets({
      climate::CLIMATE_PRESET_AWAY,
  });

  return traits;
}

// ==========================================================================
// Allow Control helper
// ==========================================================================
bool AbcdEspComponent::is_control_allowed() const {
  return allow_control_lock_ != nullptr &&
         allow_control_lock_->state == lock::LOCK_STATE_UNLOCKED;
}

// ==========================================================================
// Climate control — handle HA calls to change mode/setpoint/fan
// ==========================================================================
void AbcdEspComponent::control(const climate::ClimateCall &call) {
  // Block control when Allow Control lock is locked (or not configured)
  if (!is_control_allowed()) {
    ESP_LOGW(TAG, "Control blocked: Allow Control is locked");
    return;
  }

  // Build a 3B03 write payload with flag header
  // Layout: [0]=zone_bitmap, [1-2]=field_flags(BE), [3..]=zone data
  // We only write zone 1 fields.

  // Start by reading current cached state into the write buffer
  // We need at least 28 bytes (through cool setpoints)
  memset(write_buf_, 0, sizeof(write_buf_));
  write_buf_[0] = 0x01;  // zone bitmap: zone 1 only
  write_len_ = 28;        // minimum payload through cool setpoints

  uint16_t flags = 0;

  // Current values as defaults
  uint8_t new_fan = fan_mode_;
  uint8_t new_hold = zone_hold_;
  uint8_t new_heat = heat_setpoint_;
  uint8_t new_cool = cool_setpoint_;
  uint8_t new_mode = current_mode_;

  if (call.get_mode().has_value()) {
    switch (*call.get_mode()) {
      case climate::CLIMATE_MODE_OFF:
        new_mode = MODE_OFF;
        break;
      case climate::CLIMATE_MODE_HEAT:
        new_mode = MODE_HEAT;
        break;
      case climate::CLIMATE_MODE_COOL:
        new_mode = MODE_COOL;
        break;
      case climate::CLIMATE_MODE_HEAT_COOL:
        new_mode = MODE_AUTO;
        break;
      default:
        break;
    }
    // Mode is written via 3B02 WRITE, not 3B03
    // We'll handle mode writes separately below
  }

  if (call.get_fan_mode().has_value()) {
    switch (*call.get_fan_mode()) {
      case climate::CLIMATE_FAN_AUTO:
        new_fan = FAN_AUTO;
        break;
      case climate::CLIMATE_FAN_LOW:
        new_fan = FAN_LOW;
        break;
      case climate::CLIMATE_FAN_MEDIUM:
        new_fan = FAN_MED;
        break;
      case climate::CLIMATE_FAN_HIGH:
        new_fan = FAN_HIGH;
        break;
      default:
        break;
    }
    flags |= 0x0001;  // fan mode flag
  }

  // Single target temperature (heat or cool mode)
  if (call.get_target_temperature().has_value()) {
    uint8_t target = static_cast<uint8_t>(c_to_f(*call.get_target_temperature()) + 0.5f);
    // Determine which setpoint based on effective mode
    uint8_t effective_mode = call.get_mode().has_value() ? new_mode : current_mode_;
    if (effective_mode == MODE_HEAT) {
      new_heat = target;
      flags |= 0x0004;  // heat setpoint flag
    } else if (effective_mode == MODE_COOL) {
      new_cool = target;
      flags |= 0x0008;  // cool setpoint flag
    }
  }

  // Dual target temperatures (auto/heat_cool mode)
  if (call.get_target_temperature_low().has_value()) {
    new_heat = static_cast<uint8_t>(c_to_f(*call.get_target_temperature_low()) + 0.5f);
    flags |= 0x0004;  // heat setpoint flag
  }

  if (call.get_target_temperature_high().has_value()) {
    new_cool = static_cast<uint8_t>(c_to_f(*call.get_target_temperature_high()) + 0.5f);
    flags |= 0x0008;  // cool setpoint flag
  }

  // Handle mode change via 3B02 WRITE
  if (call.get_mode().has_value()) {
    uint8_t mode_buf[29];
    memset(mode_buf, 0, sizeof(mode_buf));
    mode_buf[0] = 0x01;               // zone bitmap
    mode_buf[1] = 0x00;               // flags high
    mode_buf[2] = 0x10;               // flags low — mode field (bit 4)
    mode_buf[22] = new_mode;
    send_write_request(ADDR_TSTAT, TBL_SAM_INFO, ROW_SAM_STATE,
                       mode_buf, 29);
    ESP_LOGI(TAG, "Setting mode to %d", new_mode);
    // Note: current_mode_ is NOT updated here. The next poll will confirm.
  }

  // Send 3B03 if any zone settings changed
  if (flags != 0) {
    write_buf_[1] = (flags >> 8) & 0xFF;
    write_buf_[2] = flags & 0xFF;

    // Fan modes for zones 1-8 (offsets 3-10)
    write_buf_[3] = new_fan;
    for (int i = 1; i < 8; i++) {
      write_buf_[3 + i] = FAN_AUTO;
    }

    // Hold flag (offset 11) — only set hold when setpoints changed
    bool setpoints_changed = (flags & 0x000C) != 0;  // heat or cool setpoint flags
    if (setpoints_changed) {
      write_buf_[11] = 0x01;
      flags |= 0x0002;

      // Determine effective hold duration: number entity overrides compile-time config
      uint16_t dur = (hold_duration_number_ != nullptr)
                         ? static_cast<uint16_t>(hold_duration_number_->state)
                         : hold_duration_minutes_;

      // Native timed hold: set override fields so the thermostat manages the
      // countdown.  Fall back to the ESP timer if dur is 0 (permanent hold)
      // or if the write payload is too short.
      if (dur > 0) {
        // Timed override flag (byte 37): set zone 1 bit
        write_buf_[37] = 0x01;
        flags |= 0x0040;  // timed override flag field

        // Override duration for zone 1 (bytes 38-39, uint16 BE, minutes)
        write_buf_[38] = (dur >> 8) & 0xFF;
        write_buf_[39] = dur & 0xFF;
        flags |= 0x0080;  // override time field

        write_len_ = 54;  // need bytes through offset 53 (8 zones x 2 bytes)

        ESP_LOGI(TAG, "Setting native timed hold for %d minutes", dur);
      }

      // Always set ESP timer as fallback in case native override isn't honored
      hold_set_ms_ = millis();
    }
    write_buf_[1] = (flags >> 8) & 0xFF;
    write_buf_[2] = flags & 0xFF;

    // Heat setpoints (offsets 12-19)
    write_buf_[12] = new_heat;
    for (int i = 1; i < 8; i++) {
      write_buf_[12 + i] = new_heat;
    }

    // Cool setpoints (offsets 20-27)
    write_buf_[20] = new_cool;
    for (int i = 1; i < 8; i++) {
      write_buf_[20 + i] = new_cool;
    }

    write_pending_ = true;

    ESP_LOGI(TAG, "Queued 3B03 write: fan=%d heat=%d cool=%d", new_fan,
             new_heat, new_cool);
  }

  // Note: state is NOT optimistically updated here.
  // The next poll of 3B02/3B03 will confirm the thermostat accepted the change.

  // Handle preset change (vacation via 3B04)
  if (call.get_preset().has_value()) {
    auto preset = *call.get_preset();
    if (preset == climate::CLIMATE_PRESET_AWAY && !vacation_active_) {
      // Read vacation parameters from number entities, falling back to defaults
      uint8_t vac_days = 7;
      uint8_t vac_min_temp = 60;
      uint8_t vac_max_temp = 80;

      if (vacation_days_number_ != nullptr && !std::isnan(vacation_days_number_->state)) {
        vac_days = static_cast<uint8_t>(vacation_days_number_->state);
      }
      if (vacation_min_temp_number_ != nullptr && !std::isnan(vacation_min_temp_number_->state)) {
        vac_min_temp = static_cast<uint8_t>(vacation_min_temp_number_->state);
      }
      if (vacation_max_temp_number_ != nullptr && !std::isnan(vacation_max_temp_number_->state)) {
        vac_max_temp = static_cast<uint8_t>(vacation_max_temp_number_->state);
      }

      uint16_t days_x7 = static_cast<uint16_t>(vac_days) * 7;
      uint8_t vac_buf[8];
      vac_buf[0] = 0x01;                       // vacation_active = 1
      vac_buf[1] = (days_x7 >> 8) & 0xFF;      // days*7 high byte
      vac_buf[2] = days_x7 & 0xFF;             // days*7 low byte
      vac_buf[3] = vac_min_temp;               // min temp °F
      vac_buf[4] = vac_max_temp;               // max temp °F
      vac_buf[5] = 15;                         // min humidity
      vac_buf[6] = 60;                         // max humidity
      vac_buf[7] = FAN_AUTO;
      send_write_request(ADDR_TSTAT, TBL_SAM_INFO, ROW_SAM_VACATION,
                         vac_buf, 8);
      ESP_LOGI(TAG, "Activating vacation mode (%d days, %d-%dF)",
               vac_days, vac_min_temp, vac_max_temp);
    } else if (preset == climate::CLIMATE_PRESET_AWAY && vacation_active_) {
      // Deactivate vacation
      uint8_t vac_buf[8];
      memset(vac_buf, 0, sizeof(vac_buf));
      vac_buf[0] = 0x00;  // vacation_active = 0
      send_write_request(ADDR_TSTAT, TBL_SAM_INFO, ROW_SAM_VACATION,
                         vac_buf, 8);
      ESP_LOGI(TAG, "Deactivating vacation mode");
    }
  }
}

// ==========================================================================
// Publish climate entity state to HA
// ==========================================================================
void AbcdEspComponent::publish_climate_state() {
  // Current temperature (convert °F → °C for HA)
  if (!std::isnan(indoor_temp_)) {
    this->current_temperature = f_to_c(indoor_temp_);
  }

  // Current humidity
  this->current_humidity = static_cast<float>(indoor_humidity_);

  // Mode
  switch (current_mode_) {
    case MODE_HEAT:
      this->mode = climate::CLIMATE_MODE_HEAT;
      break;
    case MODE_COOL:
      this->mode = climate::CLIMATE_MODE_COOL;
      break;
    case MODE_AUTO:
      this->mode = climate::CLIMATE_MODE_HEAT_COOL;
      break;
    case MODE_OFF:
    default:
      this->mode = climate::CLIMATE_MODE_OFF;
      break;
  }

  // Target temperatures (convert °F → °C for HA)
  // Use single target in heat/cool modes, dual target in auto mode
  switch (this->mode) {
    case climate::CLIMATE_MODE_HEAT:
      this->target_temperature = f_to_c(static_cast<float>(heat_setpoint_));
      break;
    case climate::CLIMATE_MODE_COOL:
      this->target_temperature = f_to_c(static_cast<float>(cool_setpoint_));
      break;
    case climate::CLIMATE_MODE_HEAT_COOL:
      this->target_temperature_low = f_to_c(static_cast<float>(heat_setpoint_));
      this->target_temperature_high = f_to_c(static_cast<float>(cool_setpoint_));
      break;
    default:
      break;
  }

  // Fan mode
  switch (fan_mode_) {
    case FAN_LOW:
      this->fan_mode = climate::CLIMATE_FAN_LOW;
      break;
    case FAN_MED:
      this->fan_mode = climate::CLIMATE_FAN_MEDIUM;
      break;
    case FAN_HIGH:
      this->fan_mode = climate::CLIMATE_FAN_HIGH;
      break;
    case FAN_AUTO:
    default:
      this->fan_mode = climate::CLIMATE_FAN_AUTO;
      break;
  }

  // Action — use real stage data from air handler and heat pump
  if (current_mode_ == MODE_OFF) {
    this->action = climate::CLIMATE_ACTION_OFF;
  } else if (heat_stage_ > 0) {
    this->action = climate::CLIMATE_ACTION_HEATING;
  } else if (hp_stage_ > 0 && current_mode_ == MODE_COOL) {
    this->action = climate::CLIMATE_ACTION_COOLING;
  } else if (hp_stage_ > 0 && current_mode_ == MODE_AUTO) {
    // Heat pump active in auto mode — determine direction from coil temp
    // If coil is hotter than outdoor, it's heating; otherwise cooling
    if (!std::isnan(hp_coil_temp_) && !std::isnan(outdoor_temp_) &&
        hp_coil_temp_ > outdoor_temp_ + 10.0f) {
      this->action = climate::CLIMATE_ACTION_HEATING;
    } else {
      this->action = climate::CLIMATE_ACTION_COOLING;
    }
  } else if (hp_stage_ > 0 && current_mode_ == MODE_HEAT) {
    this->action = climate::CLIMATE_ACTION_HEATING;
  } else if (blower_running_) {
    this->action = climate::CLIMATE_ACTION_FAN;
  } else {
    this->action = climate::CLIMATE_ACTION_IDLE;
  }

  // Preset — only show Away when vacation is active
  if (vacation_active_) {
    this->preset = climate::CLIMATE_PRESET_AWAY;
  } else {
    this->preset.reset();
  }

  this->publish_state();
}

// ==========================================================================
// Publish sensor entities
// ==========================================================================
void AbcdEspComponent::publish_sensors() {
  if (outdoor_temp_sensor_ != nullptr && !std::isnan(outdoor_temp_) &&
      (std::isnan(prev_outdoor_temp_) || outdoor_temp_ != prev_outdoor_temp_)) {
    outdoor_temp_sensor_->publish_state(outdoor_temp_);
    prev_outdoor_temp_ = outdoor_temp_;
  }

  if (airflow_cfm_sensor_ != nullptr && airflow_cfm_ != prev_airflow_cfm_) {
    airflow_cfm_sensor_->publish_state(static_cast<float>(airflow_cfm_));
    prev_airflow_cfm_ = airflow_cfm_;
  }

  if (blower_sensor_ != nullptr && blower_running_ != prev_blower_running_) {
    blower_sensor_->publish_state(blower_running_);
    prev_blower_running_ = blower_running_;
  }

  if (heat_stage_ != prev_heat_stage_) {
    if (heat_stage_sensor_ != nullptr) {
      heat_stage_sensor_->publish_state(static_cast<float>(heat_stage_));
    }
    if (heat_stage_text_sensor_ != nullptr) {
      static const char *const kHeatStageLabels[] = {"Off", "Low", "Med", "High"};
      uint8_t idx = (heat_stage_ <= 3) ? heat_stage_ : 0;
      heat_stage_text_sensor_->publish_state(kHeatStageLabels[idx]);
    }
    prev_heat_stage_ = heat_stage_;
  }

  if (indoor_humidity_sensor_ != nullptr && indoor_humidity_ != prev_indoor_humidity_) {
    indoor_humidity_sensor_->publish_state(static_cast<float>(indoor_humidity_));
    prev_indoor_humidity_ = indoor_humidity_;
  }

  if (hp_coil_temp_sensor_ != nullptr && !std::isnan(hp_coil_temp_) &&
      (std::isnan(prev_hp_coil_temp_) || hp_coil_temp_ != prev_hp_coil_temp_)) {
    hp_coil_temp_sensor_->publish_state(hp_coil_temp_);
    prev_hp_coil_temp_ = hp_coil_temp_;
  }

  if (hp_stage_ != prev_hp_stage_) {
    if (hp_stage_sensor_ != nullptr) {
      hp_stage_sensor_->publish_state(static_cast<float>(hp_stage_));
    }
    if (hp_stage_text_sensor_ != nullptr) {
      static const char *const kHpStageLabels[] = {"Off", "Low", "High"};
      uint8_t idx = (hp_stage_ <= 2) ? hp_stage_ : 0;
      hp_stage_text_sensor_->publish_state(kHpStageLabels[idx]);
    }
    prev_hp_stage_ = hp_stage_;
  }
}

// ==========================================================================
// Dump config
// ==========================================================================
void AbcdEspComponent::dump_config() {
  ESP_LOGCONFIG(TAG, "ABCDESP HVAC:");
  ESP_LOGCONFIG(TAG, "  SAM Address: 0x%04X", ADDR_SAM);
  ESP_LOGCONFIG(TAG, "  Thermostat Address: 0x%04X", ADDR_TSTAT);
  ESP_LOGCONFIG(TAG, "  Poll Interval: %d ms", POLL_INTERVAL_MS);
  if (hold_duration_minutes_ > 0) {
    ESP_LOGCONFIG(TAG, "  Hold Duration: %d minutes (temporary)", hold_duration_minutes_);
  } else {
    ESP_LOGCONFIG(TAG, "  Hold Duration: permanent");
  }
  if (flow_pin_ != nullptr) {
    LOG_PIN("  Flow Control Pin: ", flow_pin_);
  }
  LOG_SENSOR("  ", "Outdoor Temp", outdoor_temp_sensor_);
  LOG_SENSOR("  ", "Airflow CFM", airflow_cfm_sensor_);
  LOG_BINARY_SENSOR("  ", "Blower", blower_sensor_);
  LOG_SENSOR("  ", "Heat Stage", heat_stage_sensor_);
  LOG_TEXT_SENSOR("  ", "Heat Stage Label", heat_stage_text_sensor_);
  LOG_SENSOR("  ", "Indoor Humidity", indoor_humidity_sensor_);
  LOG_SENSOR("  ", "HP Coil Temp", hp_coil_temp_sensor_);
  LOG_SENSOR("  ", "HP Stage", hp_stage_sensor_);
  LOG_TEXT_SENSOR("  ", "HP Stage Label", hp_stage_text_sensor_);
  LOG_BINARY_SENSOR("  ", "Comms OK", comms_ok_sensor_);
  LOG_BINARY_SENSOR("  ", "Hold Active", hold_active_sensor_);
  LOG_SENSOR("  ", "Hold Time Remaining", hold_time_remaining_sensor_);
  if (clear_hold_button_ != nullptr) {
    ESP_LOGCONFIG(TAG, "  Clear Hold Button: configured");
  }
  if (allow_control_lock_ != nullptr) {
    ESP_LOGCONFIG(TAG, "  Allow Control Lock: configured");
  } else {
    ESP_LOGCONFIG(TAG, "  Allow Control Lock: not configured (read-only)");
  }
  if (hold_duration_number_ != nullptr) {
    ESP_LOGCONFIG(TAG, "  Hold Duration Number: configured");
  }
  if (set_hold_time_number_ != nullptr) {
    ESP_LOGCONFIG(TAG, "  Set Hold Time Number: configured");
  }
  if (vacation_days_number_ != nullptr) {
    ESP_LOGCONFIG(TAG, "  Vacation Days Number: configured");
  }
  if (vacation_min_temp_number_ != nullptr) {
    ESP_LOGCONFIG(TAG, "  Vacation Min Temp Number: configured");
  }
  if (vacation_max_temp_number_ != nullptr) {
    ESP_LOGCONFIG(TAG, "  Vacation Max Temp Number: configured");
  }
  if (last_seen_sensor_ != nullptr) {
    ESP_LOGCONFIG(TAG, "  Last Seen Sensor: configured");
  }
}

// ==========================================================================
// Adjust hold — change remaining time on an active hold (or make permanent)
// ==========================================================================
void AbcdEspComponent::adjust_hold(uint16_t minutes) {
  if (!is_control_allowed()) {
    ESP_LOGW(TAG, "Adjust hold blocked: Allow Control is locked");
    return;
  }

  if ((zone_hold_ & 0x01) == 0) {
    ESP_LOGW(TAG, "Adjust hold blocked: no active hold on zone 1");
    return;
  }

  memset(write_buf_, 0, sizeof(write_buf_));
  write_buf_[0] = 0x01;  // zone bitmap: zone 1

  if (minutes > 0) {
    // Timed hold: set hold + timed override + duration
    uint16_t flags = 0x0002 | 0x0040 | 0x0080;
    write_buf_[1] = (flags >> 8) & 0xFF;
    write_buf_[2] = flags & 0xFF;
    write_buf_[11] = 0x01;  // keep hold active
    write_buf_[37] = 0x01;  // timed override zone 1
    write_buf_[38] = (minutes >> 8) & 0xFF;
    write_buf_[39] = minutes & 0xFF;

    hold_set_ms_ = millis();  // start ESP fallback timer
    ESP_LOGI(TAG, "Adjusting hold to %d minutes", minutes);
  } else {
    // Permanent hold: clear timed override but keep hold
    uint16_t flags = 0x0002 | 0x0040 | 0x0080;
    write_buf_[1] = (flags >> 8) & 0xFF;
    write_buf_[2] = flags & 0xFF;
    write_buf_[11] = 0x01;  // keep hold active
    write_buf_[37] = 0x00;  // clear timed override
    write_buf_[38] = 0x00;
    write_buf_[39] = 0x00;

    hold_set_ms_ = 0;  // cancel ESP fallback timer
    ESP_LOGI(TAG, "Adjusting hold to permanent");
  }

  write_len_ = 54;
  write_pending_ = true;
}

// ==========================================================================
// Clear hold — send 3B03 write clearing the hold flag for zone 1
// ==========================================================================
void AbcdEspComponent::clear_hold() {
  if (!is_control_allowed()) {
    ESP_LOGW(TAG, "Clear hold blocked: Allow Control is locked");
    return;
  }

  memset(write_buf_, 0, sizeof(write_buf_));
  write_buf_[0] = 0x01;   // zone bitmap: zone 1
  // flags: hold flag (0x0002) + timed override flag (0x0040) + override time (0x0080)
  uint16_t flags = 0x0002 | 0x0040 | 0x0080;
  write_buf_[1] = (flags >> 8) & 0xFF;
  write_buf_[2] = flags & 0xFF;
  // hold bitmap (offset 11) = 0x00 — clear hold for zone 1
  write_buf_[11] = 0x00;
  // timed override flag (offset 37) = 0x00 — clear timed override
  write_buf_[37] = 0x00;
  // override time (offsets 38-39) = 0 minutes
  write_buf_[38] = 0x00;
  write_buf_[39] = 0x00;

  write_len_ = 54;  // include override fields
  write_pending_ = true;
  hold_set_ms_ = 0;  // cancel any pending ESP-managed auto-clear

  ESP_LOGI(TAG, "Queued clear hold for zone 1");
}

}  // namespace abcdesp
}  // namespace esphome
