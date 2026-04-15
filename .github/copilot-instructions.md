# Copilot Instructions

## C/C++ Coding Style

Follow the [Google C++ Style Guide](https://google.github.io/styleguide/cppguide.html) with the following rules emphasized:

- **Always use curly braces** for `if`, `else`, `for`, `while`, and `do` bodies — even single-line bodies. No braceless control structures.
- Use `snake_case` for variables and functions, `kConstantName` for constants, `ClassName` for types.
- Prefer `const` wherever applicable.
- Use `uint8_t`, `uint16_t`, etc. (from `<cstdint>`) instead of bare `int` for protocol byte fields.

## Testing

- **Write or update tests** in `tests/test_protocol.cpp` for every change to protocol logic in `abcdesp.cpp`/`abcdesp.h`.
- Keep protocol-layer functions (CRC, framing, parsing, state machine logic) decoupled from ESPHome/Arduino APIs so they can compile and run on the host with `g++` under `-DUNIT_TEST`.
- Use the existing `TEST(name)` / `ASSERT_EQ` / `ASSERT_TRUE` / `PASS()` macros already in the test file.
- Tests are compiled with `-Wall -Wextra -Werror`; all warnings must be clean.

## Project Context

This is an ESPHome custom component (`AbcdEspComponent`) that bridges an ESP32 to a Carrier Infinity HVAC system over an RS-485-like serial bus. The protocol is reverse-engineered and described in `README.md`. Be conservative with protocol changes — incorrect bytes sent to the HVAC system can cause real hardware damage.
