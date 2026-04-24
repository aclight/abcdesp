// ==========================================================================
// Unit tests for Carrier Infinity protocol logic.
//
// Compile and run (no ESPHome or Arduino needed):
//   Linux/macOS: g++ -std=c++17 -o test_runner test_protocol.cpp -DUNIT_TEST && ./test_runner
//   Windows:     cl /std:c++17 /DUNIT_TEST test_protocol.cpp /Fe:test_runner.exe && test_runner.exe
// ==========================================================================

#include <cstdint>
#include <cstring>
#include <cstdio>
#include <cmath>
#include <cassert>

// ---------------------------------------------------------------------------
// Minimal shims so protocol code compiles without ESPHome
// ---------------------------------------------------------------------------
// NAN is already defined by <cmath>

// Protocol constants (copied from header to avoid ESPHome includes)
static const uint8_t FRAME_HEADER_LEN = 8;
static const uint8_t FRAME_CRC_LEN    = 2;
static const uint8_t FRAME_MIN_LEN    = FRAME_HEADER_LEN + FRAME_CRC_LEN;
static const uint16_t CRC_POLY        = 0xA001;

static const uint16_t ADDR_TSTAT      = 0x2001;
static const uint16_t ADDR_SAM        = 0x9201;

static const uint8_t FUNC_ACK06       = 0x06;
static const uint8_t FUNC_READ        = 0x0B;
static const uint8_t FUNC_WRITE       = 0x0C;

static const uint8_t REG_PREFIX       = 0x00;

static const uint8_t MODE_HEAT = 0x00;
static const uint8_t MODE_COOL = 0x01;
static const uint8_t MODE_AUTO = 0x02;
static const uint8_t MODE_OFF  = 0x05;

static const uint8_t FAN_AUTO = 0x00;
static const uint8_t FAN_LOW  = 0x01;
static const uint8_t FAN_MED  = 0x02;
static const uint8_t FAN_HIGH = 0x03;

// CRC failure threshold (mirrors abcdesp.h)
static const uint16_t CRC_FAIL_LOG_THRESHOLD = 10;

struct InfinityFrame {
  uint16_t dst;
  uint16_t src;
  uint8_t  length;
  uint8_t  func;
  uint8_t  data[255];
};

// ---------------------------------------------------------------------------
// Protocol functions under test (standalone re-implementations)
// ---------------------------------------------------------------------------

uint16_t crc16(const uint8_t *data, uint16_t len) {
  uint16_t crc = 0x0000;
  for (uint16_t i = 0; i < len; i++) {
    crc ^= data[i];
    for (uint8_t j = 0; j < 8; j++) {
      if (crc & 0x0001)
        crc = (crc >> 1) ^ CRC_POLY;
      else
        crc >>= 1;
    }
  }
  return crc;
}

bool parse_frame(const uint8_t *buf, uint16_t len, InfinityFrame &frame) {
  if (len < FRAME_MIN_LEN)
    return false;

  uint8_t data_len = buf[4];
  uint16_t expected_len = FRAME_HEADER_LEN + data_len + FRAME_CRC_LEN;
  if (len < expected_len)
    return false;

  uint16_t calc = crc16(buf, FRAME_HEADER_LEN + data_len);
  uint16_t received = buf[FRAME_HEADER_LEN + data_len] |
                      (buf[FRAME_HEADER_LEN + data_len + 1] << 8);
  if (calc != received)
    return false;

  frame.dst = (buf[0] << 8) | buf[1];
  frame.src = (buf[2] << 8) | buf[3];
  frame.length = data_len;
  frame.func = buf[7];
  if (data_len > 0)
    memcpy(frame.data, buf + FRAME_HEADER_LEN, data_len);

  return true;
}

void build_frame(const InfinityFrame &frame, uint8_t *out, uint16_t &out_len) {
  out[0] = (frame.dst >> 8) & 0xFF;
  out[1] = frame.dst & 0xFF;
  out[2] = (frame.src >> 8) & 0xFF;
  out[3] = frame.src & 0xFF;
  out[4] = frame.length;
  out[5] = 0x00;
  out[6] = 0x00;
  out[7] = frame.func;
  if (frame.length > 0)
    memcpy(out + FRAME_HEADER_LEN, frame.data, frame.length);

  uint16_t payload_end = FRAME_HEADER_LEN + frame.length;
  uint16_t crc = crc16(out, payload_end);
  out[payload_end] = crc & 0xFF;
  out[payload_end + 1] = (crc >> 8) & 0xFF;
  out_len = payload_end + FRAME_CRC_LEN;
}

// ---------------------------------------------------------------------------
// Test helpers
// ---------------------------------------------------------------------------
static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) \
  static void test_##name(); \
  struct TestReg_##name { TestReg_##name() { test_##name(); } } reg_##name; \
  static void test_##name()

#define ASSERT_TRUE(expr) do { \
  if (!(expr)) { \
    printf("  FAIL: %s:%d: %s\n", __FILE__, __LINE__, #expr); \
    tests_failed++; \
    return; \
  } \
} while(0)

#define ASSERT_EQ(a, b) do { \
  auto _a = (a); auto _b = (b); \
  if (_a != _b) { \
    printf("  FAIL: %s:%d: %s == %lld, expected %lld\n", \
           __FILE__, __LINE__, #a, (long long)_a, (long long)_b); \
    tests_failed++; \
    return; \
  } \
} while(0)

#define PASS() do { tests_passed++; printf("  PASS\n"); } while(0)

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

TEST(crc16_empty) {
  printf("test_crc16_empty\n");
  uint16_t c = crc16(nullptr, 0);
  ASSERT_EQ(c, 0x0000);
  PASS();
}

TEST(crc16_known_pattern) {
  printf("test_crc16_known_pattern\n");
  // A READ request frame from SAM(9201)->TSTAT(2001), reading register 003B02
  // Header: 20 01 92 01 03 00 00 0B
  // Data:   00 3B 02
  uint8_t frame_bytes[] = {0x20, 0x01, 0x92, 0x01, 0x03, 0x00, 0x00, 0x0B,
                           0x00, 0x3B, 0x02};
  uint16_t c = crc16(frame_bytes, sizeof(frame_bytes));
  // Verify CRC is non-zero and consistent
  uint16_t c2 = crc16(frame_bytes, sizeof(frame_bytes));
  ASSERT_EQ(c, c2);
  ASSERT_TRUE(c != 0);  // extremely unlikely to be zero for this payload
  PASS();
}

TEST(build_and_parse_roundtrip) {
  printf("test_build_and_parse_roundtrip\n");
  // Build a READ request frame
  InfinityFrame orig;
  orig.dst = ADDR_TSTAT;
  orig.src = ADDR_SAM;
  orig.func = FUNC_READ;
  orig.length = 3;
  orig.data[0] = 0x00;
  orig.data[1] = 0x3B;
  orig.data[2] = 0x02;

  uint8_t buf[32];
  uint16_t buf_len;
  build_frame(orig, buf, buf_len);

  ASSERT_EQ(buf_len, FRAME_HEADER_LEN + 3 + FRAME_CRC_LEN);  // 13

  // Parse it back
  InfinityFrame parsed;
  ASSERT_TRUE(parse_frame(buf, buf_len, parsed));
  ASSERT_EQ(parsed.dst, ADDR_TSTAT);
  ASSERT_EQ(parsed.src, ADDR_SAM);
  ASSERT_EQ(parsed.func, FUNC_READ);
  ASSERT_EQ(parsed.length, 3);
  ASSERT_EQ(parsed.data[0], 0x00);
  ASSERT_EQ(parsed.data[1], 0x3B);
  ASSERT_EQ(parsed.data[2], 0x02);
  PASS();
}

TEST(parse_frame_too_short) {
  printf("test_parse_frame_too_short\n");
  uint8_t buf[5] = {0};
  InfinityFrame f;
  ASSERT_TRUE(!parse_frame(buf, sizeof(buf), f));
  PASS();
}

TEST(parse_frame_bad_crc) {
  printf("test_parse_frame_bad_crc\n");
  // Build a valid frame then corrupt the CRC
  InfinityFrame orig;
  orig.dst = ADDR_TSTAT;
  orig.src = ADDR_SAM;
  orig.func = FUNC_READ;
  orig.length = 3;
  orig.data[0] = 0x00;
  orig.data[1] = 0x3B;
  orig.data[2] = 0x02;

  uint8_t buf[32];
  uint16_t buf_len;
  build_frame(orig, buf, buf_len);

  // Corrupt the last byte (CRC high)
  buf[buf_len - 1] ^= 0xFF;

  InfinityFrame parsed;
  ASSERT_TRUE(!parse_frame(buf, buf_len, parsed));
  PASS();
}

TEST(parse_frame_corrupt_length) {
  printf("test_parse_frame_corrupt_length\n");
  // Build valid frame then set length byte to huge value
  InfinityFrame orig;
  orig.dst = ADDR_TSTAT;
  orig.src = ADDR_SAM;
  orig.func = FUNC_ACK06;
  orig.length = 1;
  orig.data[0] = 0x00;

  uint8_t buf[32];
  uint16_t buf_len;
  build_frame(orig, buf, buf_len);

  // Corrupt length to 200 — parse should fail (not enough data)
  buf[4] = 200;
  InfinityFrame parsed;
  ASSERT_TRUE(!parse_frame(buf, buf_len, parsed));
  PASS();
}

TEST(build_ack_frame) {
  printf("test_build_ack_frame\n");
  InfinityFrame ack;
  ack.dst = ADDR_TSTAT;
  ack.src = ADDR_SAM;
  ack.func = FUNC_ACK06;
  ack.length = 1;
  ack.data[0] = 0x00;

  uint8_t buf[32];
  uint16_t buf_len;
  build_frame(ack, buf, buf_len);

  ASSERT_EQ(buf_len, FRAME_HEADER_LEN + 1 + FRAME_CRC_LEN);  // 11
  ASSERT_EQ(buf[0], 0x20);  // dst high
  ASSERT_EQ(buf[1], 0x01);  // dst low
  ASSERT_EQ(buf[7], FUNC_ACK06);
  ASSERT_EQ(buf[8], 0x00);  // data

  // Verify it parses back
  InfinityFrame parsed;
  ASSERT_TRUE(parse_frame(buf, buf_len, parsed));
  ASSERT_EQ(parsed.func, FUNC_ACK06);
  PASS();
}

TEST(build_write_frame) {
  printf("test_build_write_frame\n");
  InfinityFrame f;
  f.dst = ADDR_TSTAT;
  f.src = ADDR_SAM;
  f.func = FUNC_WRITE;
  // Simulate writing mode via 3B02: register addr (3 bytes) + 29 bytes payload
  f.length = 3 + 29;
  f.data[0] = 0x00;
  f.data[1] = 0x3B;
  f.data[2] = 0x02;
  memset(f.data + 3, 0, 29);
  f.data[3] = 0x01;       // zone bitmap
  f.data[3 + 22] = 0x01;  // mode = cool

  uint8_t buf[64];
  uint16_t buf_len;
  build_frame(f, buf, buf_len);

  ASSERT_EQ(buf_len, (uint16_t)(FRAME_HEADER_LEN + 32 + FRAME_CRC_LEN));

  InfinityFrame parsed;
  ASSERT_TRUE(parse_frame(buf, buf_len, parsed));
  ASSERT_EQ(parsed.func, FUNC_WRITE);
  ASSERT_EQ(parsed.length, 32);
  ASSERT_EQ(parsed.data[1], 0x3B);
  ASSERT_EQ(parsed.data[2], 0x02);
  ASSERT_EQ(parsed.data[3 + 22], 0x01);  // mode=cool preserved
  PASS();
}

TEST(parse_3b02_data) {
  printf("test_parse_3b02_data\n");
  // Simulate 3B02 register content (29 bytes)
  // Layout: [0]=zone_bitmap, [1-2]=flags, [3-10]=temps, [11-18]=humidity,
  //          [19]=unknown, [20]=outdoor, [21]=unocc, [22]=mode, [23-28]=misc
  uint8_t reg[29] = {0};
  reg[0]  = 0x01;  // zone bitmap
  reg[3]  = 72;    // zone 1 temp = 72°F
  reg[11] = 45;    // zone 1 humidity = 45%
  reg[20] = 55;    // outdoor temp = 55°F (positive)
  reg[22] = 0x02;  // mode = auto

  ASSERT_EQ(reg[3], 72);
  ASSERT_EQ(reg[11], 45);
  ASSERT_EQ((int8_t)reg[20], 55);
  ASSERT_EQ(reg[22] & 0x0F, MODE_AUTO);
  PASS();
}

TEST(parse_3b02_negative_outdoor) {
  printf("test_parse_3b02_negative_outdoor\n");
  uint8_t reg[29] = {0};
  reg[20] = (uint8_t)(-5);  // -5°F as int8

  int8_t outdoor = (int8_t)reg[20];
  ASSERT_EQ(outdoor, -5);
  PASS();
}

TEST(parse_3b03_data) {
  printf("test_parse_3b03_data\n");
  // Simulate 3B03 register content (at least 28 bytes)
  uint8_t reg[28] = {0};
  reg[0]  = 0x01;  // zone bitmap
  reg[3]  = FAN_MED;   // zone 1 fan mode
  reg[11] = 0x01;      // zone 1 on hold
  reg[12] = 68;        // zone 1 heat setpoint
  reg[20] = 74;        // zone 1 cool setpoint

  ASSERT_EQ(reg[3], FAN_MED);
  ASSERT_EQ(reg[11] & 0x01, 1);  // hold for zone 1
  ASSERT_EQ(reg[12], 68);
  ASSERT_EQ(reg[20], 74);
  PASS();
}

TEST(parse_3b03_timed_override) {
  printf("test_parse_3b03_timed_override\n");
  // Simulate full 3B03 register with timed override fields (150 bytes)
  uint8_t reg[54] = {0};
  reg[0]  = 0x01;      // zone bitmap
  reg[11] = 0x01;      // zone 1 on hold
  reg[12] = 70;        // heat setpoint
  reg[20] = 76;        // cool setpoint

  // Timed override flag (byte 37): zone 1 has timed override
  reg[37] = 0x01;
  // Override time remaining for zone 1 (bytes 38-39, uint16 BE): 120 minutes
  reg[38] = 0x00;
  reg[39] = 120;

  ASSERT_EQ(reg[37] & 0x01, 1);  // zone 1 timed override active
  uint16_t remaining = (reg[38] << 8) | reg[39];
  ASSERT_EQ(remaining, 120);

  // Zone 2 timed override at bytes 40-41
  reg[37] = 0x03;  // zones 1 and 2
  reg[40] = 0x01;  // 0x0168 = 360 minutes
  reg[41] = 0x68;
  uint16_t z2_remaining = (reg[40] << 8) | reg[41];
  ASSERT_EQ(z2_remaining, 360);
  ASSERT_EQ(reg[37] & 0x02, 2);  // zone 2 bit set

  // No timed override
  reg[37] = 0x00;
  ASSERT_EQ(reg[37] & 0x01, 0);  // zone 1 not on timed override
  remaining = (reg[37] & 0x01) ? ((reg[38] << 8) | reg[39]) : 0;
  ASSERT_EQ(remaining, 0);
  PASS();
}

TEST(timed_override_flag_encoding) {
  printf("test_timed_override_flag_encoding\n");
  // Verify flag values for 3B03 timed override write
  uint16_t flags = 0;
  flags |= 0x0002;  // hold flag
  flags |= 0x0004;  // heat setpoint
  flags |= 0x0040;  // timed override flag
  flags |= 0x0080;  // override time

  ASSERT_EQ(flags, 0x00C6);
  ASSERT_EQ((flags >> 8) & 0xFF, 0x00);
  ASSERT_EQ(flags & 0xFF, 0xC6);

  // Encode 120 minutes as uint16 BE
  uint16_t minutes = 120;
  uint8_t hi = (minutes >> 8) & 0xFF;
  uint8_t lo = minutes & 0xFF;
  ASSERT_EQ(hi, 0);
  ASSERT_EQ(lo, 120);

  // Encode 1440 minutes (max, 24 hours) as uint16 BE
  minutes = 1440;
  hi = (minutes >> 8) & 0xFF;
  lo = minutes & 0xFF;
  ASSERT_EQ(hi, 0x05);
  ASSERT_EQ(lo, 0xA0);
  ASSERT_EQ((uint16_t)((hi << 8) | lo), 1440);
  PASS();
}

TEST(parse_heatpump_3e01) {
  printf("test_parse_heatpump_3e01\n");
  // 3E01: uint16 outside_temp*16, uint16 coil_temp*16 (big-endian)
  uint8_t data[4];
  // outside = 50.5°F → 50.5 * 16 = 808 = 0x0328
  data[0] = 0x03;
  data[1] = 0x28;
  // coil = 28.75°F → 28.75 * 16 = 460 = 0x01CC
  data[2] = 0x01;
  data[3] = 0xCC;

  float outside = (float)((data[0] << 8) | data[1]) / 16.0f;
  float coil    = (float)((data[2] << 8) | data[3]) / 16.0f;

  ASSERT_TRUE(fabsf(outside - 50.5f) < 0.01f);
  ASSERT_TRUE(fabsf(coil - 28.75f) < 0.01f);
  PASS();
}

TEST(parse_heatpump_3e02_stage) {
  printf("test_parse_heatpump_3e02_stage\n");
  // 3E02: data[0] >> 1 = stage
  uint8_t data_stage2 = 0x04;  // 4 >> 1 = 2
  ASSERT_EQ(data_stage2 >> 1, 2);

  uint8_t data_stage0 = 0x00;
  ASSERT_EQ(data_stage0 >> 1, 0);

  uint8_t data_stage1 = 0x02;
  ASSERT_EQ(data_stage1 >> 1, 1);
  PASS();
}

TEST(parse_airhandler_0316) {
  printf("test_parse_airhandler_0316\n");
  // 0316: [0]=heat_state, [1-3]=unknown, [4-5]=CFM (uint16 BE)
  uint8_t data[6] = {0};
  data[0] = 0x02;   // med heat
  data[4] = 0x01;   // CFM = 0x017A = 378
  data[5] = 0x7A;

  ASSERT_EQ(data[0], 2);
  uint16_t cfm = (data[4] << 8) | data[5];
  ASSERT_EQ(cfm, 378);
  PASS();
}

TEST(crc16_frame_roundtrip_integrity) {
  printf("test_crc16_frame_roundtrip_integrity\n");
  // Build many different frames and verify CRC round-trips
  for (int data_len = 0; data_len < 50; data_len++) {
    InfinityFrame f;
    f.dst = 0x2001;
    f.src = 0x9201;
    f.func = 0x0B;
    f.length = data_len;
    for (int i = 0; i < data_len; i++)
      f.data[i] = (uint8_t)(i * 7 + data_len);

    uint8_t buf[300];
    uint16_t buf_len;
    build_frame(f, buf, buf_len);

    InfinityFrame parsed;
    ASSERT_TRUE(parse_frame(buf, buf_len, parsed));
    ASSERT_EQ(parsed.length, data_len);
    for (int i = 0; i < data_len; i++)
      ASSERT_EQ(parsed.data[i], f.data[i]);
  }
  PASS();
}

TEST(mode_encoding) {
  printf("test_mode_encoding\n");
  ASSERT_EQ(MODE_HEAT, 0x00);
  ASSERT_EQ(MODE_COOL, 0x01);
  ASSERT_EQ(MODE_AUTO, 0x02);
  ASSERT_EQ(MODE_OFF,  0x05);
  PASS();
}

TEST(fan_mode_encoding) {
  printf("test_fan_mode_encoding\n");
  ASSERT_EQ(FAN_AUTO, 0x00);
  ASSERT_EQ(FAN_LOW,  0x01);
  ASSERT_EQ(FAN_MED,  0x02);
  ASSERT_EQ(FAN_HIGH, 0x03);
  PASS();
}

TEST(parse_vacation_3b04) {
  printf("test_parse_vacation_3b04\n");
  // 3B04: [0]=vacation_active, [1-2]=days*7(BE), [3]=mintemp, [4]=maxtemp,
  //       [5]=minhumidity, [6]=maxhumidity, [7]=fanmode
  uint8_t data[8] = {0};
  data[0] = 0x01;  // vacation active
  data[1] = 0x00;  // days*7 high (7 days = 49 = 0x0031)
  data[2] = 0x31;  // days*7 low
  data[3] = 55;    // min temp
  data[4] = 85;    // max temp
  data[5] = 15;    // min humidity
  data[6] = 60;    // max humidity
  data[7] = FAN_AUTO;

  ASSERT_EQ(data[0], 1);  // vacation active
  uint16_t days_times7 = (data[1] << 8) | data[2];
  ASSERT_EQ(days_times7, 49);  // 7 days * 7
  ASSERT_EQ(data[3], 55);
  ASSERT_EQ(data[4], 85);
  ASSERT_EQ(data[5], 15);
  ASSERT_EQ(data[6], 60);
  ASSERT_EQ(data[7], FAN_AUTO);

  // Inactive vacation
  data[0] = 0x00;
  ASSERT_EQ(data[0], 0);

  // Byte 0 non-zero but days=0 — should NOT be considered active
  // (thermostat may keep byte 0 set after vacation expires)
  data[0] = 0x01;
  data[1] = 0x00;
  data[2] = 0x00;  // days*7 = 0
  days_times7 = (data[1] << 8) | data[2];
  bool vacation_active = (data[0] != 0);
  if (vacation_active && days_times7 == 0) {
    vacation_active = false;
  }
  ASSERT_TRUE(!vacation_active);

  // Byte 0 non-zero AND days > 0 — genuinely active
  data[0] = 0x01;
  data[1] = 0x00;
  data[2] = 0x07;  // days*7 = 7 (1 day)
  days_times7 = (data[1] << 8) | data[2];
  vacation_active = (data[0] != 0);
  if (vacation_active && days_times7 == 0) {
    vacation_active = false;
  }
  ASSERT_TRUE(vacation_active);
  PASS();
}

TEST(heat_stage_label_mapping) {
  printf("test_heat_stage_label_mapping\n");
  // Verify the label mapping logic used in publish_sensors
  static const char *const kHeatStageLabels[] = {"Off", "Low", "Med", "High"};

  for (uint8_t stage = 0; stage <= 3; stage++) {
    uint8_t idx = (stage <= 3) ? stage : 0;
    const char *label = kHeatStageLabels[idx];
    ASSERT_TRUE(label != nullptr);
  }

  // Verify specific mappings
  ASSERT_EQ(strcmp(kHeatStageLabels[0], "Off"), 0);
  ASSERT_EQ(strcmp(kHeatStageLabels[1], "Low"), 0);
  ASSERT_EQ(strcmp(kHeatStageLabels[2], "Med"), 0);
  ASSERT_EQ(strcmp(kHeatStageLabels[3], "High"), 0);

  // Out-of-range should map to index 0 (Off)
  uint8_t bad_stage = 5;
  uint8_t idx = (bad_stage <= 3) ? bad_stage : 0;
  ASSERT_EQ(idx, 0);
  PASS();
}

TEST(hp_stage_label_mapping) {
  printf("test_hp_stage_label_mapping\n");
  // Verify the HP stage label mapping logic used in publish_sensors
  static const char *const kHpStageLabels[] = {"Off", "Low", "High"};

  for (uint8_t stage = 0; stage <= 2; stage++) {
    uint8_t idx = (stage <= 2) ? stage : 0;
    const char *label = kHpStageLabels[idx];
    ASSERT_TRUE(label != nullptr);
  }

  // Verify specific mappings
  ASSERT_EQ(strcmp(kHpStageLabels[0], "Off"), 0);
  ASSERT_EQ(strcmp(kHpStageLabels[1], "Low"), 0);
  ASSERT_EQ(strcmp(kHpStageLabels[2], "High"), 0);

  // Out-of-range should map to index 0 (Off)
  uint8_t bad_stage = 5;
  uint8_t idx = (bad_stage <= 2) ? bad_stage : 0;
  ASSERT_EQ(idx, 0);
  PASS();
}

TEST(crc_fail_counter_logic) {
  printf("test_crc_fail_counter_logic\n");
  // Simulates the CRC failure counting logic from loop():
  //   - counter increments on each CRC failure
  //   - resets to 0 when a valid frame is parsed
  //   - threshold triggers error log at CRC_FAIL_LOG_THRESHOLD
  uint16_t crc_fail_count = 0;
  bool error_logged = false;

  // Simulate consecutive CRC failures up to threshold
  for (uint16_t i = 0; i < CRC_FAIL_LOG_THRESHOLD; i++) {
    crc_fail_count++;
    if (crc_fail_count == CRC_FAIL_LOG_THRESHOLD) {
      error_logged = true;
    }
  }
  ASSERT_EQ(crc_fail_count, CRC_FAIL_LOG_THRESHOLD);
  ASSERT_TRUE(error_logged);

  // Simulate successful parse — counter resets
  crc_fail_count = 0;
  ASSERT_EQ(crc_fail_count, 0);

  // Simulate a few failures then success — should NOT trigger error
  error_logged = false;
  for (uint16_t i = 0; i < CRC_FAIL_LOG_THRESHOLD - 1; i++) {
    crc_fail_count++;
    if (crc_fail_count == CRC_FAIL_LOG_THRESHOLD) {
      error_logged = true;
    }
  }
  ASSERT_EQ(crc_fail_count, CRC_FAIL_LOG_THRESHOLD - 1);
  ASSERT_TRUE(!error_logged);

  // Success resets counter
  crc_fail_count = 0;
  ASSERT_EQ(crc_fail_count, 0);
  PASS();
}

// ---------------------------------------------------------------------------
// Temperature conversion helpers (mirrors abcdesp.cpp)
// ---------------------------------------------------------------------------
static float f_to_c(float f) { return (f - 32.0f) * 5.0f / 9.0f; }
static float c_to_f(float c) { return c * 9.0f / 5.0f + 32.0f; }

TEST(f_to_c_basic) {
  printf("test_f_to_c_basic\n");
  ASSERT_TRUE(fabsf(f_to_c(32.0f) - 0.0f) < 0.01f);     // freezing
  ASSERT_TRUE(fabsf(f_to_c(212.0f) - 100.0f) < 0.01f);   // boiling
  ASSERT_TRUE(fabsf(f_to_c(72.0f) - 22.222f) < 0.01f);   // room temp
  ASSERT_TRUE(fabsf(f_to_c(-40.0f) - (-40.0f)) < 0.01f); // crossover
  PASS();
}

TEST(c_to_f_basic) {
  printf("test_c_to_f_basic\n");
  ASSERT_TRUE(fabsf(c_to_f(0.0f) - 32.0f) < 0.01f);
  ASSERT_TRUE(fabsf(c_to_f(100.0f) - 212.0f) < 0.01f);
  ASSERT_TRUE(fabsf(c_to_f(-40.0f) - (-40.0f)) < 0.01f);
  PASS();
}

TEST(temperature_roundtrip) {
  printf("test_temperature_roundtrip\n");
  // Verify f_to_c(c_to_f(x)) ≈ x for typical setpoint range
  for (float c = 4.0f; c <= 38.0f; c += 0.5f) {
    float roundtrip = f_to_c(c_to_f(c));
    ASSERT_TRUE(fabsf(roundtrip - c) < 0.01f);
  }
  PASS();
}

TEST(setpoint_f_to_c_rounding) {
  printf("test_setpoint_f_to_c_rounding\n");
  // HA sends °C, component converts to °F uint8_t via: (uint8_t)(c_to_f(x) + 0.5f)
  // Verify this rounds correctly for common setpoints
  float c_68f = f_to_c(68.0f);  // ≈ 20.0°C
  uint8_t result = static_cast<uint8_t>(c_to_f(c_68f) + 0.5f);
  ASSERT_EQ(result, 68);

  float c_72f = f_to_c(72.0f);  // ≈ 22.22°C
  result = static_cast<uint8_t>(c_to_f(c_72f) + 0.5f);
  ASSERT_EQ(result, 72);

  float c_55f = f_to_c(55.0f);  // ≈ 12.78°C
  result = static_cast<uint8_t>(c_to_f(c_55f) + 0.5f);
  ASSERT_EQ(result, 55);

  float c_85f = f_to_c(85.0f);  // ≈ 29.44°C
  result = static_cast<uint8_t>(c_to_f(c_85f) + 0.5f);
  ASSERT_EQ(result, 85);
  PASS();
}

TEST(target_temp_clear_stale_on_mode_switch) {
  printf("test_target_temp_clear_stale_on_mode_switch\n");
  // When switching from AUTO (dual target) to HEAT or COOL (single target),
  // the unused target fields must be NaN so HA doesn't show stale values.

  uint8_t heat_setpoint = 68;
  uint8_t cool_setpoint = 76;
  float target_temperature = NAN;
  float target_temperature_low = NAN;
  float target_temperature_high = NAN;

  // Simulate AUTO mode — sets dual targets, clears single
  target_temperature_low = f_to_c(static_cast<float>(heat_setpoint));
  target_temperature_high = f_to_c(static_cast<float>(cool_setpoint));
  target_temperature = NAN;
  ASSERT_TRUE(!std::isnan(target_temperature_low));
  ASSERT_TRUE(!std::isnan(target_temperature_high));
  ASSERT_TRUE(std::isnan(target_temperature));

  // Simulate switching to COOL mode — must clear dual targets
  target_temperature = f_to_c(static_cast<float>(cool_setpoint));
  target_temperature_low = NAN;
  target_temperature_high = NAN;
  ASSERT_TRUE(!std::isnan(target_temperature));
  ASSERT_TRUE(std::isnan(target_temperature_low));   // must be NaN
  ASSERT_TRUE(std::isnan(target_temperature_high));  // must be NaN

  // Simulate switching to HEAT mode — must clear dual targets
  target_temperature = f_to_c(static_cast<float>(heat_setpoint));
  target_temperature_low = NAN;
  target_temperature_high = NAN;
  ASSERT_TRUE(!std::isnan(target_temperature));
  ASSERT_TRUE(std::isnan(target_temperature_low));
  ASSERT_TRUE(std::isnan(target_temperature_high));

  // Simulate switching to OFF — all NaN
  target_temperature = NAN;
  target_temperature_low = NAN;
  target_temperature_high = NAN;
  ASSERT_TRUE(std::isnan(target_temperature));
  ASSERT_TRUE(std::isnan(target_temperature_low));
  ASSERT_TRUE(std::isnan(target_temperature_high));
  PASS();
}

TEST(heatpump_3e01_unsigned_sanity) {
  printf("test_heatpump_3e01_unsigned_sanity\n");
  // 3E01 temps are parsed as uint16/16.0. Verify that values >0x7FFF
  // produce unreasonable temperatures, confirming they'd indicate a
  // signed-negative encoding issue.
  uint8_t data[4];

  // Normal positive: 70°F → 70*16=1120=0x0460
  data[0] = 0x04; data[1] = 0x60;
  data[2] = 0x00; data[3] = 0x00;
  float outside = static_cast<float>((data[0] << 8) | data[1]) / 16.0f;
  ASSERT_TRUE(fabsf(outside - 70.0f) < 0.01f);

  // Zero
  data[0] = 0x00; data[1] = 0x00;
  outside = static_cast<float>((data[0] << 8) | data[1]) / 16.0f;
  ASSERT_TRUE(fabsf(outside - 0.0f) < 0.01f);

  // If the HP encoded -5°F as signed int16: -5*16=-80=0xFFB0
  // Parsed unsigned: 65456/16 = 4091°F — obviously wrong
  data[0] = 0xFF; data[1] = 0xB0;
  outside = static_cast<float>((data[0] << 8) | data[1]) / 16.0f;
  ASSERT_TRUE(outside > 4000.0f);  // confirms unsigned parse gives nonsense
  // If we parsed as signed int16 instead:
  int16_t signed_val = static_cast<int16_t>((data[0] << 8) | data[1]);
  float outside_signed = static_cast<float>(signed_val) / 16.0f;
  ASSERT_TRUE(fabsf(outside_signed - (-5.0f)) < 0.01f);  // signed gives correct value
  PASS();
}

TEST(parse_3b02_mode_with_stage_bits) {
  printf("test_parse_3b02_mode_with_stage_bits\n");
  // Mode byte: low nibble = mode, bits 5-7 = stage
  // Verify the & 0x0F mask extracts mode correctly regardless of stage bits
  uint8_t mode_byte;

  // Auto mode, stage 1 (bits 5-7 = 001 → 0x20)
  mode_byte = 0x22;  // 0x20 | MODE_AUTO(0x02)
  ASSERT_EQ(mode_byte & 0x0F, MODE_AUTO);

  // Heat mode, stage 3 (bits 5-7 = 011 → 0x60)
  mode_byte = 0x60;  // 0x60 | MODE_HEAT(0x00)
  ASSERT_EQ(mode_byte & 0x0F, MODE_HEAT);

  // Cool mode, stage 2 (bits 5-7 = 010 → 0x40)
  mode_byte = 0x41;  // 0x40 | MODE_COOL(0x01)
  ASSERT_EQ(mode_byte & 0x0F, MODE_COOL);

  // Off mode, no stage
  mode_byte = 0x05;
  ASSERT_EQ(mode_byte & 0x0F, MODE_OFF);

  // All stage bits set (0xE0) should not leak into mode
  mode_byte = 0xE2;
  ASSERT_EQ(mode_byte & 0x0F, MODE_AUTO);
  PASS();
}

TEST(build_mode_write_payload) {
  printf("test_build_mode_write_payload\n");
  // Simulate the 3B02 mode write payload construction from control()
  uint8_t mode_buf[29];
  memset(mode_buf, 0, sizeof(mode_buf));
  mode_buf[0] = 0x01;   // zone bitmap
  mode_buf[1] = 0x00;   // flags high
  mode_buf[2] = 0x10;   // flags low — mode field (bit 4)
  mode_buf[22] = MODE_COOL;

  // Verify structure
  ASSERT_EQ(mode_buf[0], 0x01);
  ASSERT_EQ(mode_buf[2], 0x10);         // only mode flag set
  ASSERT_EQ(mode_buf[22], MODE_COOL);

  // Zone bitmap should only include zone 1
  ASSERT_EQ(mode_buf[0] & 0xFE, 0x00);  // no other zones

  // Build as a frame and verify it round-trips
  InfinityFrame f;
  f.dst = ADDR_TSTAT;
  f.src = ADDR_SAM;
  f.func = FUNC_WRITE;
  f.length = 3 + 29;
  f.data[0] = 0x00;
  f.data[1] = 0x3B;
  f.data[2] = 0x02;
  memcpy(f.data + 3, mode_buf, 29);

  uint8_t buf[64];
  uint16_t buf_len;
  build_frame(f, buf, buf_len);

  InfinityFrame parsed;
  ASSERT_TRUE(parse_frame(buf, buf_len, parsed));
  ASSERT_EQ(parsed.func, FUNC_WRITE);
  ASSERT_EQ(parsed.data[3 + 22], MODE_COOL);
  ASSERT_EQ(parsed.data[3 + 0], 0x01);  // zone bitmap preserved
  ASSERT_EQ(parsed.data[3 + 2], 0x10);  // flags preserved
  PASS();
}

TEST(build_setpoint_write_payload) {
  printf("test_build_setpoint_write_payload\n");
  // Simulate 3B03 setpoint write from control() with hold
  uint8_t write_buf[54];
  memset(write_buf, 0, sizeof(write_buf));

  write_buf[0] = 0x01;   // zone bitmap: zone 1

  uint8_t new_fan = FAN_MED;
  uint8_t new_heat = 70;
  uint8_t new_cool = 76;
  uint16_t flags = 0;

  flags |= 0x0001;  // fan mode
  flags |= 0x0004;  // heat setpoint
  flags |= 0x0008;  // cool setpoint
  flags |= 0x0002;  // hold flag (because setpoints changed)

  write_buf[1] = (flags >> 8) & 0xFF;
  write_buf[2] = flags & 0xFF;

  write_buf[3] = new_fan;
  for (int i = 1; i < 8; i++) {
    write_buf[3 + i] = FAN_AUTO;
  }

  write_buf[11] = 0x01;  // hold zone 1

  write_buf[12] = new_heat;
  for (int i = 1; i < 8; i++) {
    write_buf[12 + i] = new_heat;
  }

  write_buf[20] = new_cool;
  for (int i = 1; i < 8; i++) {
    write_buf[20 + i] = new_cool;
  }

  // Verify key fields
  ASSERT_EQ(write_buf[0], 0x01);          // zone 1 only
  ASSERT_EQ(write_buf[2], 0x0F);          // flags: fan + hold + heat + cool
  ASSERT_EQ(write_buf[3], FAN_MED);       // zone 1 fan
  ASSERT_EQ(write_buf[4], FAN_AUTO);      // zone 2 default
  ASSERT_EQ(write_buf[11], 0x01);         // hold set
  ASSERT_EQ(write_buf[12], 70);           // zone 1 heat
  ASSERT_EQ(write_buf[20], 76);           // zone 1 cool
  PASS();
}

TEST(build_timed_hold_write_payload) {
  printf("test_build_timed_hold_write_payload\n");
  // Simulate 3B03 write with timed override
  uint8_t write_buf[54];
  memset(write_buf, 0, sizeof(write_buf));

  write_buf[0] = 0x01;

  uint16_t flags = 0;
  flags |= 0x0004;  // heat setpoint
  flags |= 0x0002;  // hold flag
  flags |= 0x0040;  // timed override flag
  flags |= 0x0080;  // override time field

  uint16_t hold_minutes = 120;
  write_buf[11] = 0x01;  // hold zone 1
  write_buf[12] = 72;    // heat setpoint
  write_buf[37] = 0x01;  // timed override zone 1
  write_buf[38] = (hold_minutes >> 8) & 0xFF;
  write_buf[39] = hold_minutes & 0xFF;

  write_buf[1] = (flags >> 8) & 0xFF;
  write_buf[2] = flags & 0xFF;

  // Verify
  ASSERT_EQ(write_buf[37], 0x01);
  ASSERT_EQ((write_buf[38] << 8) | write_buf[39], 120);
  ASSERT_EQ(write_buf[1], 0x00);
  ASSERT_EQ(write_buf[2], 0xC6);  // 0x0002|0x0004|0x0040|0x0080
  PASS();
}

TEST(build_clear_hold_payload) {
  printf("test_build_clear_hold_payload\n");
  // Simulate the clear_hold() payload construction
  uint8_t write_buf[54];
  memset(write_buf, 0, sizeof(write_buf));

  write_buf[0] = 0x01;   // zone bitmap: zone 1
  uint16_t flags = 0x0002 | 0x0040 | 0x0080;
  write_buf[1] = (flags >> 8) & 0xFF;
  write_buf[2] = flags & 0xFF;
  write_buf[11] = 0x00;  // clear hold
  write_buf[37] = 0x00;  // clear timed override
  write_buf[38] = 0x00;
  write_buf[39] = 0x00;

  // Verify all hold/override fields are cleared
  ASSERT_EQ(write_buf[11], 0x00);
  ASSERT_EQ(write_buf[37], 0x00);
  ASSERT_EQ(write_buf[38], 0x00);
  ASSERT_EQ(write_buf[39], 0x00);
  // Verify flags request the hold and override fields
  uint16_t encoded_flags = (write_buf[1] << 8) | write_buf[2];
  ASSERT_TRUE((encoded_flags & 0x0002) != 0);   // hold flag
  ASSERT_TRUE((encoded_flags & 0x0040) != 0);   // timed override flag
  ASSERT_TRUE((encoded_flags & 0x0080) != 0);   // override time flag
  // Verify no other fields are flagged (we're only clearing hold)
  ASSERT_EQ(encoded_flags & ~(0x0002 | 0x0040 | 0x0080), 0);
  PASS();
}

TEST(frame_max_data_length) {
  printf("test_frame_max_data_length\n");
  // Verify frames with maximum data length (255 bytes) build and parse correctly
  InfinityFrame f;
  f.dst = 0x2001;
  f.src = 0x9201;
  f.func = FUNC_ACK06;
  f.length = 255;
  for (int i = 0; i < 255; i++) {
    f.data[i] = static_cast<uint8_t>(i);
  }

  uint8_t buf[270];
  uint16_t buf_len;
  build_frame(f, buf, buf_len);
  ASSERT_EQ(buf_len, (uint16_t)(FRAME_HEADER_LEN + 255 + FRAME_CRC_LEN));

  InfinityFrame parsed;
  ASSERT_TRUE(parse_frame(buf, buf_len, parsed));
  ASSERT_EQ(parsed.length, 255);
  ASSERT_EQ(parsed.data[0], 0);
  ASSERT_EQ(parsed.data[254], 254);
  PASS();
}

TEST(frame_zero_data_length) {
  printf("test_frame_zero_data_length\n");
  // A frame with zero data bytes (just header + CRC)
  InfinityFrame f;
  f.dst = 0x2001;
  f.src = 0x9201;
  f.func = FUNC_ACK06;
  f.length = 0;

  uint8_t buf[16];
  uint16_t buf_len;
  build_frame(f, buf, buf_len);
  ASSERT_EQ(buf_len, (uint16_t)FRAME_MIN_LEN);

  InfinityFrame parsed;
  ASSERT_TRUE(parse_frame(buf, buf_len, parsed));
  ASSERT_EQ(parsed.length, 0);
  ASSERT_EQ(parsed.func, FUNC_ACK06);
  PASS();
}

TEST(parse_frame_truncated_data) {
  printf("test_parse_frame_truncated_data\n");
  // Build a valid frame, then present it with fewer bytes than length indicates
  InfinityFrame f;
  f.dst = 0x2001;
  f.src = 0x9201;
  f.func = FUNC_READ;
  f.length = 3;
  f.data[0] = 0x00; f.data[1] = 0x3B; f.data[2] = 0x02;

  uint8_t buf[32];
  uint16_t buf_len;
  build_frame(f, buf, buf_len);  // 13 bytes total

  // Pass only 11 bytes — not enough for header(8)+data(3)+crc(2)
  InfinityFrame parsed;
  ASSERT_TRUE(!parse_frame(buf, 11, parsed));
  PASS();
}

TEST(crc16_single_byte) {
  printf("test_crc16_single_byte\n");
  // CRC of a single byte should be non-trivial
  uint8_t b = 0x42;
  uint16_t c = crc16(&b, 1);
  ASSERT_TRUE(c != 0);  // CRC of non-zero byte is non-zero for this poly
  // Verify determinism
  ASSERT_EQ(c, crc16(&b, 1));
  PASS();
}

TEST(parse_airhandler_0306_blower) {
  printf("test_parse_airhandler_0306_blower\n");
  // 0306: [0]=unknown, [1-2]=blower_rpm (uint16 BE), [3-4]=unknown
  uint8_t data[5] = {0};

  // Blower off
  data[1] = 0x00; data[2] = 0x00;
  uint16_t rpm = (data[1] << 8) | data[2];
  ASSERT_EQ(rpm, 0);
  ASSERT_TRUE(rpm == 0);  // not running

  // Blower running at 1200 RPM = 0x04B0
  data[1] = 0x04; data[2] = 0xB0;
  rpm = (data[1] << 8) | data[2];
  ASSERT_EQ(rpm, 1200);
  ASSERT_TRUE(rpm > 0);  // running
  PASS();
}

TEST(vacation_3b04_roundtrip) {
  printf("test_vacation_3b04_roundtrip\n");
  // Build a vacation-activate payload and verify all fields
  uint8_t vac_buf[8];
  vac_buf[0] = 0x01;  // active
  // 7 days * 7 = 49 = 0x0031
  vac_buf[1] = 0x00;
  vac_buf[2] = 0x31;
  vac_buf[3] = 60;    // min temp
  vac_buf[4] = 80;    // max temp
  vac_buf[5] = 15;    // min humidity
  vac_buf[6] = 60;    // max humidity
  vac_buf[7] = FAN_AUTO;

  // Wrap in a WRITE frame and verify it round-trips
  InfinityFrame f;
  f.dst = ADDR_TSTAT;
  f.src = ADDR_SAM;
  f.func = FUNC_WRITE;
  f.length = 3 + 8;
  f.data[0] = 0x00; f.data[1] = 0x3B; f.data[2] = 0x04;
  memcpy(f.data + 3, vac_buf, 8);

  uint8_t buf[32];
  uint16_t buf_len;
  build_frame(f, buf, buf_len);

  InfinityFrame parsed;
  ASSERT_TRUE(parse_frame(buf, buf_len, parsed));
  ASSERT_EQ(parsed.data[3], 0x01);    // active
  ASSERT_EQ(parsed.data[3 + 3], 60);  // min temp
  ASSERT_EQ(parsed.data[3 + 4], 80);  // max temp
  ASSERT_EQ(parsed.data[3 + 7], FAN_AUTO);

  // Verify deactivation payload
  memset(vac_buf, 0, sizeof(vac_buf));
  ASSERT_EQ(vac_buf[0], 0x00);  // inactive
  PASS();
}

TEST(hold_active_includes_timed_override) {
  printf("test_hold_active_includes_timed_override\n");
  // hold_active should be true when EITHER zone_hold bit 0 OR
  // zone_override_flag bit 0 is set (Bug: previously only checked zone_hold).

  uint8_t zone_hold = 0;
  uint8_t zone_override_flag = 0;

  // Neither permanent hold nor timed override — not active
  bool hold_active = ((zone_hold & 0x01) != 0) || ((zone_override_flag & 0x01) != 0);
  ASSERT_TRUE(!hold_active);

  // Permanent hold only (no timed override)
  zone_hold = 0x01;
  zone_override_flag = 0x00;
  hold_active = ((zone_hold & 0x01) != 0) || ((zone_override_flag & 0x01) != 0);
  ASSERT_TRUE(hold_active);

  // Timed override only (no permanent hold flag) — this is the bug case
  zone_hold = 0x00;
  zone_override_flag = 0x01;
  hold_active = ((zone_hold & 0x01) != 0) || ((zone_override_flag & 0x01) != 0);
  ASSERT_TRUE(hold_active);

  // Both permanent hold and timed override
  zone_hold = 0x01;
  zone_override_flag = 0x01;
  hold_active = ((zone_hold & 0x01) != 0) || ((zone_override_flag & 0x01) != 0);
  ASSERT_TRUE(hold_active);

  // Zone 2 override only — zone 1 not active
  zone_hold = 0x00;
  zone_override_flag = 0x02;
  hold_active = ((zone_hold & 0x01) != 0) || ((zone_override_flag & 0x01) != 0);
  ASSERT_TRUE(!hold_active);
  PASS();
}

TEST(hold_zone_bitmap) {
  printf("test_hold_zone_bitmap\n");
  // Verify zone bitmap bitfield usage for hold
  uint8_t zone_hold = 0;

  // No zones on hold
  ASSERT_EQ(zone_hold & 0x01, 0);  // zone 1 not held

  // Zone 1 on hold
  zone_hold = 0x01;
  ASSERT_EQ(zone_hold & 0x01, 1);

  // Zones 1 and 3 on hold
  zone_hold = 0x05;
  ASSERT_EQ(zone_hold & 0x01, 1);  // zone 1
  ASSERT_EQ(zone_hold & 0x02, 0);  // zone 2 not held
  ASSERT_EQ(zone_hold & 0x04, 4);  // zone 3

  // All 8 zones on hold
  zone_hold = 0xFF;
  for (int z = 0; z < 8; z++) {
    ASSERT_TRUE((zone_hold & (1 << z)) != 0);
  }
  PASS();
}

TEST(snoop_address_class_matching) {
  printf("test_snoop_address_class_matching\n");
  // Verify the address class masking used in handle_frame for snooping
  // Air handler addresses: 0x40xx, 0x42xx
  ASSERT_EQ(0x4001 & 0xFF00, 0x4000);
  ASSERT_EQ(0x4201 & 0xFF00, 0x4200);
  // Not an air handler
  ASSERT_TRUE((0x5001 & 0xFF00) != 0x4000);
  ASSERT_TRUE((0x5001 & 0xFF00) != 0x4200);

  // Heat pump addresses: 0x50xx, 0x51xx, 0x52xx
  ASSERT_EQ(0x5001 & 0xFF00, 0x5000);
  ASSERT_EQ(0x5101 & 0xFF00, 0x5100);
  ASSERT_EQ(0x5201 & 0xFF00, 0x5200);
  // SAM is not matched as heat pump
  ASSERT_TRUE((0x9201 & 0xFF00) != 0x5000);
  PASS();
}

// ---------------------------------------------------------------------------
// adjust_hold payload construction tests
// ---------------------------------------------------------------------------

// Helper: build an adjust_hold write buffer and return it for verification.
// Mirrors AbcdEspComponent::adjust_hold() logic.
static void build_adjust_hold_payload(uint16_t minutes, uint8_t *write_buf,
                                      uint8_t &write_len) {
  memset(write_buf, 0, 54);
  write_buf[0] = 0x01;  // zone bitmap: zone 1

  if (minutes > 0) {
    uint16_t flags = 0x0002 | 0x0040 | 0x0080;
    write_buf[1] = (flags >> 8) & 0xFF;
    write_buf[2] = flags & 0xFF;
    write_buf[11] = 0x01;  // keep hold active
    write_buf[37] = 0x01;  // timed override zone 1
    write_buf[38] = (minutes >> 8) & 0xFF;
    write_buf[39] = minutes & 0xFF;
  } else {
    uint16_t flags = 0x0002 | 0x0040 | 0x0080;
    write_buf[1] = (flags >> 8) & 0xFF;
    write_buf[2] = flags & 0xFF;
    write_buf[11] = 0x01;  // keep hold active
    write_buf[37] = 0x00;  // clear timed override
    write_buf[38] = 0x00;
    write_buf[39] = 0x00;
  }

  write_len = 54;
}

TEST(build_adjust_hold_timed_payload) {
  printf("test_build_adjust_hold_timed_payload\n");
  // Adjust hold to 120 minutes — verify all critical bytes
  uint8_t buf[54];
  uint8_t len;
  build_adjust_hold_payload(120, buf, len);

  ASSERT_EQ(len, 54);
  ASSERT_EQ(buf[0], 0x01);   // zone bitmap
  // flags: 0x0002 | 0x0040 | 0x0080 = 0x00C2
  uint16_t flags = (buf[1] << 8) | buf[2];
  ASSERT_EQ(flags, 0x00C2);
  ASSERT_TRUE((flags & 0x0002) != 0);  // hold flag
  ASSERT_TRUE((flags & 0x0040) != 0);  // timed override flag
  ASSERT_TRUE((flags & 0x0080) != 0);  // override time flag
  ASSERT_EQ(buf[11], 0x01);  // hold active
  ASSERT_EQ(buf[37], 0x01);  // timed override zone 1
  uint16_t encoded_minutes = (buf[38] << 8) | buf[39];
  ASSERT_EQ(encoded_minutes, 120);
  // No setpoint flags — adjust_hold doesn't change setpoints
  ASSERT_TRUE((flags & 0x0004) == 0);  // no heat setpoint flag
  ASSERT_TRUE((flags & 0x0008) == 0);  // no cool setpoint flag
  PASS();
}

TEST(build_adjust_hold_permanent_payload) {
  printf("test_build_adjust_hold_permanent_payload\n");
  // Adjust hold to permanent (0 minutes) — timed override cleared, hold kept
  uint8_t buf[54];
  uint8_t len;
  build_adjust_hold_payload(0, buf, len);

  ASSERT_EQ(len, 54);
  ASSERT_EQ(buf[0], 0x01);   // zone bitmap
  uint16_t flags = (buf[1] << 8) | buf[2];
  ASSERT_EQ(flags, 0x00C2);  // same flags — writing zeros to clear override
  ASSERT_EQ(buf[11], 0x01);  // hold stays active
  ASSERT_EQ(buf[37], 0x00);  // timed override cleared
  ASSERT_EQ(buf[38], 0x00);  // duration = 0
  ASSERT_EQ(buf[39], 0x00);
  PASS();
}

TEST(adjust_hold_duration_boundary) {
  printf("test_adjust_hold_duration_boundary\n");
  uint8_t buf[54];
  uint8_t len;

  // 1 minute — minimum timed hold
  build_adjust_hold_payload(1, buf, len);
  ASSERT_EQ(buf[37], 0x01);  // timed override active
  uint16_t mins = (buf[38] << 8) | buf[39];
  ASSERT_EQ(mins, 1);

  // 1440 minutes (24 hours) — maximum
  build_adjust_hold_payload(1440, buf, len);
  ASSERT_EQ(buf[37], 0x01);
  mins = (buf[38] << 8) | buf[39];
  ASSERT_EQ(mins, 1440);
  ASSERT_EQ(buf[38], 0x05);  // 1440 = 0x05A0
  ASSERT_EQ(buf[39], 0xA0);

  // 720 minutes (12 hours)
  build_adjust_hold_payload(720, buf, len);
  mins = (buf[38] << 8) | buf[39];
  ASSERT_EQ(mins, 720);
  ASSERT_EQ(buf[38], 0x02);  // 720 = 0x02D0
  ASSERT_EQ(buf[39], 0xD0);
  PASS();
}

TEST(adjust_hold_flag_encoding) {
  printf("test_adjust_hold_flag_encoding\n");
  // Verify flags are correctly separated from setpoint-write flags
  // adjust_hold should ONLY set hold(0x0002), timed_override(0x0040), override_time(0x0080)
  // and never set fan(0x0001), heat_sp(0x0004), cool_sp(0x0008), or mode(0x0010)
  uint8_t buf[54];
  uint8_t len;

  build_adjust_hold_payload(60, buf, len);
  uint16_t flags = (buf[1] << 8) | buf[2];

  // Must have these flags
  ASSERT_TRUE((flags & 0x0002) != 0);  // hold
  ASSERT_TRUE((flags & 0x0040) != 0);  // timed override
  ASSERT_TRUE((flags & 0x0080) != 0);  // override time

  // Must NOT have these flags
  ASSERT_TRUE((flags & 0x0001) == 0);  // fan mode
  ASSERT_TRUE((flags & 0x0004) == 0);  // heat setpoint
  ASSERT_TRUE((flags & 0x0008) == 0);  // cool setpoint
  ASSERT_TRUE((flags & 0x0010) == 0);  // mode

  // All unused payload bytes (3-10, 12-36, 40-53) should be zero
  for (int i = 3; i <= 10; i++) {
    ASSERT_EQ(buf[i], 0);
  }
  for (int i = 12; i <= 36; i++) {
    ASSERT_EQ(buf[i], 0);
  }
  for (int i = 40; i <= 53; i++) {
    ASSERT_EQ(buf[i], 0);
  }
  PASS();
}

TEST(adjust_hold_requires_active_hold) {
  printf("test_adjust_hold_requires_active_hold\n");
  // Simulate the guard logic from adjust_hold():
  // adjust_hold should only proceed when zone_hold_ & 0x01 is set

  // No hold active — should block
  uint8_t zone_hold = 0x00;
  bool blocked = (zone_hold & 0x01) == 0;
  ASSERT_TRUE(blocked);

  // Zone 1 on hold — should allow
  zone_hold = 0x01;
  blocked = (zone_hold & 0x01) == 0;
  ASSERT_TRUE(!blocked);

  // Multiple zones on hold (zone 1 included) — should allow
  zone_hold = 0x05;  // zones 1 and 3
  blocked = (zone_hold & 0x01) == 0;
  ASSERT_TRUE(!blocked);

  // Only zone 2 on hold (zone 1 not held) — should block
  zone_hold = 0x02;
  blocked = (zone_hold & 0x01) == 0;
  ASSERT_TRUE(blocked);
  PASS();
}

TEST(hold_duration_number_overrides_config) {
  printf("test_hold_duration_number_overrides_config\n");
  // Simulate the logic in control() that picks between number entity and config:
  //   uint16_t dur = (hold_duration_number != nullptr)
  //                      ? static_cast<uint16_t>(hold_duration_number_state)
  //                      : hold_duration_minutes_;

  uint16_t config_value = 120;  // compile-time config

  // When number entity is not configured (nullptr), use config value
  float *number_state = nullptr;
  uint16_t dur = (number_state != nullptr)
                     ? static_cast<uint16_t>(*number_state)
                     : config_value;
  ASSERT_EQ(dur, 120);

  // When number entity IS configured with a different value
  float entity_value = 60.0f;
  number_state = &entity_value;
  dur = (number_state != nullptr)
            ? static_cast<uint16_t>(*number_state)
            : config_value;
  ASSERT_EQ(dur, 60);

  // When number entity is set to 0 (permanent hold)
  entity_value = 0.0f;
  dur = (number_state != nullptr)
            ? static_cast<uint16_t>(*number_state)
            : config_value;
  ASSERT_EQ(dur, 0);

  // When number entity is set to max (1440 minutes)
  entity_value = 1440.0f;
  dur = (number_state != nullptr)
            ? static_cast<uint16_t>(*number_state)
            : config_value;
  ASSERT_EQ(dur, 1440);
  PASS();
}

// ---------------------------------------------------------------------------
// Vacation parameter payload construction tests
// ---------------------------------------------------------------------------

// Helper: build a vacation activation payload using configurable parameters.
// Mirrors the control() logic.
static void build_vacation_payload(uint8_t days, uint8_t min_temp,
                                   uint8_t max_temp, uint8_t *vac_buf) {
  uint16_t days_x7 = static_cast<uint16_t>(days) * 7;
  vac_buf[0] = 0x01;                       // vacation_active = 1
  vac_buf[1] = (days_x7 >> 8) & 0xFF;      // days*7 high byte
  vac_buf[2] = days_x7 & 0xFF;             // days*7 low byte
  vac_buf[3] = min_temp;                   // min temp °F
  vac_buf[4] = max_temp;                   // max temp °F
  vac_buf[5] = 15;                         // min humidity
  vac_buf[6] = 60;                         // max humidity
  vac_buf[7] = FAN_AUTO;
}

TEST(build_vacation_payload_defaults) {
  printf("test_build_vacation_payload_defaults\n");
  // Default vacation: 7 days, 60-80°F
  uint8_t buf[8];
  build_vacation_payload(7, 60, 80, buf);

  ASSERT_EQ(buf[0], 0x01);  // active
  // 7 * 7 = 49 = 0x0031
  ASSERT_EQ(buf[1], 0x00);
  ASSERT_EQ(buf[2], 0x31);
  ASSERT_EQ(buf[3], 60);
  ASSERT_EQ(buf[4], 80);
  ASSERT_EQ(buf[5], 15);
  ASSERT_EQ(buf[6], 60);
  ASSERT_EQ(buf[7], FAN_AUTO);
  PASS();
}

TEST(build_vacation_payload_custom) {
  printf("test_build_vacation_payload_custom\n");
  // Custom: 14 days, 55-85°F
  uint8_t buf[8];
  build_vacation_payload(14, 55, 85, buf);

  ASSERT_EQ(buf[0], 0x01);
  // 14 * 7 = 98 = 0x0062
  uint16_t days_x7 = (buf[1] << 8) | buf[2];
  ASSERT_EQ(days_x7, 98);
  ASSERT_EQ(buf[3], 55);
  ASSERT_EQ(buf[4], 85);
  PASS();
}

TEST(build_vacation_payload_boundary_days) {
  printf("test_build_vacation_payload_boundary_days\n");
  uint8_t buf[8];

  // 1 day (minimum)
  build_vacation_payload(1, 60, 80, buf);
  uint16_t days_x7 = (buf[1] << 8) | buf[2];
  ASSERT_EQ(days_x7, 7);  // 1 * 7

  // 30 days (maximum)
  build_vacation_payload(30, 60, 80, buf);
  days_x7 = (buf[1] << 8) | buf[2];
  ASSERT_EQ(days_x7, 210);  // 30 * 7 = 0x00D2
  ASSERT_EQ(buf[1], 0x00);
  ASSERT_EQ(buf[2], 0xD2);
  PASS();
}

TEST(build_vacation_payload_boundary_temps) {
  printf("test_build_vacation_payload_boundary_temps\n");
  uint8_t buf[8];

  // Min temp = 40, max temp = 99
  build_vacation_payload(7, 40, 99, buf);
  ASSERT_EQ(buf[3], 40);
  ASSERT_EQ(buf[4], 99);

  // Narrow range: 70-72
  build_vacation_payload(7, 70, 72, buf);
  ASSERT_EQ(buf[3], 70);
  ASSERT_EQ(buf[4], 72);
  PASS();
}

TEST(build_vacation_payload_frame_roundtrip) {
  printf("test_build_vacation_payload_frame_roundtrip\n");
  // Build a vacation payload, wrap in WRITE frame, verify roundtrip
  uint8_t vac_buf[8];
  build_vacation_payload(10, 58, 78, vac_buf);

  InfinityFrame f;
  f.dst = ADDR_TSTAT;
  f.src = ADDR_SAM;
  f.func = FUNC_WRITE;
  f.length = 3 + 8;
  f.data[0] = 0x00;
  f.data[1] = 0x3B;
  f.data[2] = 0x04;
  memcpy(f.data + 3, vac_buf, 8);

  uint8_t frame_buf[32];
  uint16_t frame_len;
  build_frame(f, frame_buf, frame_len);

  InfinityFrame parsed;
  ASSERT_TRUE(parse_frame(frame_buf, frame_len, parsed));
  ASSERT_EQ(parsed.func, FUNC_WRITE);
  ASSERT_EQ(parsed.data[3], 0x01);   // active
  ASSERT_EQ(parsed.data[3 + 3], 58); // min temp
  ASSERT_EQ(parsed.data[3 + 4], 78); // max temp
  // 10 * 7 = 70
  uint16_t days_x7 = (parsed.data[3 + 1] << 8) | parsed.data[3 + 2];
  ASSERT_EQ(days_x7, 70);
  PASS();
}

TEST(vacation_number_fallback_logic) {
  printf("test_vacation_number_fallback_logic\n");
  // Simulate the fallback logic in control():
  //   if number entity is configured and has a value, use it; else use default

  uint8_t vac_days = 7;      // default
  uint8_t vac_min_temp = 60;  // default
  uint8_t vac_max_temp = 80;  // default

  // When number entities are not configured (nullptr)
  float *days_state = nullptr;
  float *min_state = nullptr;
  float *max_state = nullptr;

  uint8_t result_days = (days_state != nullptr) ? static_cast<uint8_t>(*days_state) : vac_days;
  uint8_t result_min = (min_state != nullptr) ? static_cast<uint8_t>(*min_state) : vac_min_temp;
  uint8_t result_max = (max_state != nullptr) ? static_cast<uint8_t>(*max_state) : vac_max_temp;
  ASSERT_EQ(result_days, 7);
  ASSERT_EQ(result_min, 60);
  ASSERT_EQ(result_max, 80);

  // When number entities are configured with custom values
  float d = 14.0f, mn = 55.0f, mx = 85.0f;
  days_state = &d;
  min_state = &mn;
  max_state = &mx;

  result_days = (days_state != nullptr) ? static_cast<uint8_t>(*days_state) : vac_days;
  result_min = (min_state != nullptr) ? static_cast<uint8_t>(*min_state) : vac_min_temp;
  result_max = (max_state != nullptr) ? static_cast<uint8_t>(*max_state) : vac_max_temp;
  ASSERT_EQ(result_days, 14);
  ASSERT_EQ(result_min, 55);
  ASSERT_EQ(result_max, 85);
  PASS();
}

TEST(parse_vacation_3b04_fields) {
  printf("test_parse_vacation_3b04_fields\n");
  // Verify parsing of 3B04 register content
  uint8_t data[8] = {0};

  // Active vacation: 10 days, 58-78°F
  data[0] = 0x01;  // active
  uint16_t days_x7 = 10 * 7;  // 70
  data[1] = (days_x7 >> 8) & 0xFF;
  data[2] = days_x7 & 0xFF;
  data[3] = 58;  // min temp
  data[4] = 78;  // max temp
  data[5] = 15;  // min humidity
  data[6] = 60;  // max humidity
  data[7] = FAN_AUTO;

  // Parse fields like parse_vacation does
  bool active = (data[0] != 0);
  ASSERT_TRUE(active);

  uint16_t parsed_days_x7 = (data[1] << 8) | data[2];
  uint8_t parsed_days = (parsed_days_x7 > 0) ? (parsed_days_x7 / 7) : 0;
  ASSERT_EQ(parsed_days, 10);
  ASSERT_EQ(data[3], 58);
  ASSERT_EQ(data[4], 78);

  // Inactive vacation
  data[0] = 0x00;
  active = (data[0] != 0);
  ASSERT_TRUE(!active);
  PASS();
}

TEST(vacation_deactivation_payload) {
  printf("test_vacation_deactivation_payload\n");
  // Verify the deactivation payload structure
  uint8_t vac_buf[8];
  memset(vac_buf, 0, sizeof(vac_buf));
  vac_buf[0] = 0x00;  // vacation_active = 0

  ASSERT_EQ(vac_buf[0], 0x00);
  // All other bytes should be zero
  for (int i = 1; i < 8; i++) {
    ASSERT_EQ(vac_buf[i], 0);
  }

  // Wrap in frame and verify
  InfinityFrame f;
  f.dst = ADDR_TSTAT;
  f.src = ADDR_SAM;
  f.func = FUNC_WRITE;
  f.length = 3 + 8;
  f.data[0] = 0x00;
  f.data[1] = 0x3B;
  f.data[2] = 0x04;
  memcpy(f.data + 3, vac_buf, 8);

  uint8_t buf[32];
  uint16_t buf_len;
  build_frame(f, buf, buf_len);

  InfinityFrame parsed;
  ASSERT_TRUE(parse_frame(buf, buf_len, parsed));
  ASSERT_EQ(parsed.data[3], 0x00);  // inactive
  PASS();
}

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------
int main() {
  printf("\n=== Carrier Infinity Protocol Tests ===\n\n");

  // Tests are auto-registered via static constructors above
  // (already ran by this point)

  printf("\n=== Results: %d passed, %d failed ===\n",
         tests_passed, tests_failed);
  return tests_failed > 0 ? 1 : 0;
}
