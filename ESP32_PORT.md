# Upsilon ESP32 Port

This document describes the changes made to port [Upsilon](https://github.com/UpsilonNumworks/Upsilon) (a NumWorks calculator OS) to the ESP32 microcontroller using the Xtensa toolchain.

## Overview

The ESP32 port required workarounds for several bugs and limitations in the Xtensa GCC toolchain, as well as adjustments for the ESP32's more constrained memory environment. All ESP32-specific changes are guarded by `#ifdef PLATFORM_ESP32` so the original codebase remains unaffected when building for other targets.

## Changes by Category

### 1. Xtensa Toolchain Bug Workarounds

The Xtensa compiler/toolchain has several known issues with 64-bit integer operations that required targeted workarounds:

#### Broken 64-bit shift/mask operations (`poincare/src/integer.cpp`)
- The `Integer(double_native_int_t)` constructor uses `memcpy` to extract 32-bit halves of a 64-bit value instead of the usual `j & 0xFFFFFFFF` / `j >> 32` approach, which produces incorrect results on Xtensa.
- `Integer::approximate<T>()` uses a direct cast for single-digit (inline) integers instead of the 64-bit IEEE 754 bit manipulation that fails on Xtensa.
- `Integer::serializeInDecimal()` has a fast path for inline integers that bypasses the full `udiv`-based serialization, which corrupts results on Xtensa.
- `Integer::NumberOfBase10DigitsWithoutSign()` has a similar fast path for inline integers using simple C arithmetic.
- `Integer::udiv()` has a fast path for inline integers using native C division, avoiding the full algorithm that relies on `multiplyByPowerOf2`/`multiplyByPowerOfBase`/`usum` — all of which produce corrupt results on Xtensa.

#### Broken struct member copy in derived classes (`poincare/include/poincare/integer.h`)
- The Xtensa compiler sometimes fails to copy derived-class members (`m_negative`, `m_digit`) when the base class (`TreeHandle`) has user-defined copy/move operators.
- Explicit copy constructor, move constructor, copy assignment, and move assignment operators were added to `Integer` to ensure `m_negative` and `m_digit` are always correctly copied.

#### Broken `int64_t` in Decimal rounding (`poincare/src/decimal.cpp`)
- In `Decimal::Builder(T f)`, the number of significant digits is capped at 9 (fitting in `int32_t`) instead of using `int64_t` values that Xtensa handles incorrectly.
- The mantissa is computed as `int32_t` and trailing zeros are removed with simple modular arithmetic instead of the `removeZeroAtTheEnd()` function, which relies on `Integer` assignment operators that are buggy on Xtensa.
- `IntegerDivision` in `DecimalNode::convertToText()` uses `native_int_t` casts instead of `int64_t` to avoid the broken 64-bit operations.

### 2. Memory Adjustments

#### Larger tree pool (`poincare/include/poincare/tree_pool.h`)
- `TreePool::BufferSize` is increased from 16384 to 32768 bytes on ESP32. The larger pool prevents pool exhaustion during expression evaluation on the ESP32, where the different code paths and workarounds may require more intermediate nodes.

### 3. Infinite Loop Safety

#### Serialization loop guard (`apps/calculation/calculation_store.cpp`)
- `CalculationStore::pushSerializeExpression()` has a safety break after 50 iterations on ESP32 to prevent infinite loops. If hit, it writes `"undef"` as a fallback. On real NumWorks hardware there is no such limit.

#### Digit count safety (`poincare/src/integer.cpp`)
- `Integer::NumberOfBase10DigitsWithoutSign()` caps the loop at 50 iterations on ESP32 to prevent runaway computation.

### 4. FreeRTOS Yielding

#### Cooperative multitasking delays (`apps/calculation/calculation_store.cpp`)
- `delay(1)` calls are inserted at key points during calculation push/serialization to yield to the FreeRTOS scheduler. Without these, long computations can starve the watchdog timer and trigger a reset.

### 5. Graph Rendering Adjustments

#### Skip double evaluation (`apps/graph/graph/graph_view.cpp`)
- Cartesian graph drawing skips the secondary "double" evaluation pass on ESP32 because the Xtensa toolchain produces corrupt results that cause stamps to be placed at NaN coordinates.

### 6. Serial Debug Logging

Extensive `Serial.printf()` debug logging was added throughout the codebase to aid in diagnosing the Xtensa toolchain issues. These are all guarded by `#ifdef PLATFORM_ESP32` and only output when connected to a serial monitor. Affected files:

- `apps/calculation/calculation_store.cpp` — calculation push lifecycle, serialization loop, height computation
- `apps/graph/graph/graph_view.cpp` — function evaluation, graph type detection, cartesian drawing
- `apps/shared/curve_view.cpp` — curve drawing parameters, stamp placement and clipping
- `escher/src/run_loop.cpp` — event dispatch tracing
- `poincare/src/decimal.cpp` — mantissa conversion, rounding, serialization steps
- `poincare/src/expression.cpp` — parsing, simplification, beautification, approximation pipeline
- `poincare/src/tree_pool.cpp` — pool exhaustion warnings

## Files Modified

| File | Type of Change |
|------|---------------|
| `poincare/include/poincare/integer.h` | Explicit copy/move operators for Xtensa bug |
| `poincare/include/poincare/tree_pool.h` | Larger buffer size for ESP32 |
| `poincare/src/integer.cpp` | Fast paths bypassing broken 64-bit ops |
| `poincare/src/decimal.cpp` | `int32_t` mantissa, Xtensa-safe rounding |
| `poincare/src/expression.cpp` | Debug logging for simplification pipeline |
| `poincare/src/tree_pool.cpp` | Debug logging for pool exhaustion |
| `apps/calculation/calculation_store.cpp` | Loop guard, FreeRTOS yields, debug logging |
| `apps/graph/graph/graph_view.cpp` | Skip double eval, debug logging |
| `apps/shared/curve_view.cpp` | Debug logging for curve/stamp drawing |
| `escher/src/run_loop.cpp` | Debug logging for event dispatch |

## Building

To build for ESP32, define `PLATFORM_ESP32` in your build configuration. This activates all the workarounds and debug logging described above. The project uses `<Arduino.h>` for `Serial.printf()`, `Serial.flush()`, and `delay()`, so an Arduino-compatible ESP32 toolchain is required.
