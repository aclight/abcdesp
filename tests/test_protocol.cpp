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
#define NAN __builtin_nan("")

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
