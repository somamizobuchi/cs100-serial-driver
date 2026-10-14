# CS-100 Serial Reader

An Arduino Nano that triggers a Minolta CS-100 chroma meter, decodes the meter's data output, and returns Y, x, y over USB serial. A MATLAB script reads a measurement in one call.

```
OK,57.20,0.4319,0.4150
```

## Files

| File | Purpose |
|---|---|
| `cs100_serial.ino` | Arduino sketch: trigger, frame capture, decoding, serial protocol |
| `cs100_read.m` | MATLAB script that takes one measurement and returns `Yxy = [Y x y]` |

## Hardware

- Arduino Nano (ATmega328P, CH340 USB serial). On this Mac it appears as `/dev/cu.wchusbserial1140`.
- Minolta CS-100 with the 12-pin Hirose data output connector (receptacle RP17-13RA-12SD; mating plug RP17-13P-12PC).

### Wiring

```
  Arduino Nano                          CS-100 data connector
  ────────────                          ─────────────────────
  D2  (clock in, interrupt)        ◄──── pin 10  synchronizing clock output
  D3  (trigger out)                ────► pin 11  external measure request input
  D4  (data in, internal pull-up)  ◄──── pin 8   measured value serial output
  GND                              ───── pin 1   ground (pin 2 is also ground)
```

No external components are needed. The data line is open-collector, and the Nano's internal pull-up on D4 is enough at this clock rate (tested).

The clock must be on D2 or D3, the Nano's only external-interrupt pins.

### Connector pinout

Pin positions as drawn in the LS-100/LS-110 communication manual (the manual does not say which side the drawing is viewed from):

```
               ____________
             /   2     1    \
            |  6   5   4   3 |
            | 10   9   8   7 |
             \   12    11   /
               ‾‾‾‾‾‾‾‾‾‾‾‾
```

| Pin | Signal | Notes |
|---|---|---|
| 1, 2 | Ground | |
| 3 | Busy input | Low or unconnected blocks output in the LS-100 manual; not needed on this CS-100 |
| 4 | RTS output | RS-232 (±10 V). **Do not connect to the Nano.** |
| 5 | External power input | 8–12 V, 100 mA. Leave unconnected unless powering the meter externally. |
| 6 | Data protection / CTS input | Not needed |
| 7 | TxD output | RS-232 (±10 V). **Do not connect to the Nano.** |
| 8 | Measured value serial output | Open-collector; needs a pull-up |
| 9 | RxD input | RS-232 (±10 V). **Do not connect to the Nano.** |
| 10 | Synchronizing clock output | Driven by the meter; runs only while a frame is sent |
| 11 | External measure request input | Active high; 33 kΩ series and 330 kΩ pull-down inside the meter |
| 12 | Internal supply output | 4–5 V, 10 mA max. An output: never feed voltage into it. |

The pinout comes from the LS-100/LS-110 manual, which uses the same connector. Pins 1, 8, 10 and 11 are confirmed on this CS-100 by the working setup; the others are not.

## Signal and frame format

None of this is documented by Minolta for the CS-100; it was worked out from captures.

The meter does not send text. It sends the contents of its LCD as seven-segment patterns, clocked out on pin 10.

- **Clock:** period about 2.56 ms (about 390 Hz). Data is sampled on the rising edge; rising- and falling-edge samples were identical in every capture.
- **Frame:** 256 bits, 16 words of 16 bits, about 0.65 s.
- **Word:** an 8-bit header followed by an 8-bit segment byte, each received most significant bit first.
- **Headers:** `0xF1` for words 1–15, `0x80` for the terminator word 16.
- **Segment byte:** active low. After inverting it, the bits are:

| Bit | 7 | 6 | 5 | 4 | 3 | 2 | 1 | 0 |
|---|---|---|---|---|---|---|---|---|
| Segment | decimal point | c | b | a or d | d or a | e | g | f |

(Bits 4 and 3 are a and d in some order; no digit tells them apart except possibly 7, which is decoded either way.)

### Digit patterns (inverted, decimal point bit masked off)

| Char | Pattern | Char | Pattern |
|---|---|---|---|
| 0 | `0x7D` | 5 | `0x5B` |
| 1 | `0x60` | 6 | `0x5F` |
| 2 | `0x3E` | 7 | `0x71` (also accepts `0x70`, `0x68`, `0x69`) |
| 3 | `0x7A` | 8 | `0x7F` |
| 4 | `0x63` | 9 | `0x7B` (also accepts `0x73`, `0x6B`) |
| blank | `0x00` | - | `0x02` |

Digits 0–9 have all been seen in real readings.

### Word layout

| Words | Content |
|---|---|
| 1–4 | Y, as displayed, most significant digit first. The decimal point bit marks the decimal position; Y ≥ 100 has no decimal point. |
| 5 | Leading position of x. Seen as `0x40`, and `0xC0` in a dark reading. Meaning unknown. |
| 6–8 | x, three digits after the decimal point |
| 9 | Leading position of y. Seen as `0x40`. Meaning unknown. |
| 10–12 | y, three digits after the decimal point |
| 13 | Extra digit of Y |
| 14 | Extra digit of x |
| 15 | Extra digit of y |
| 16 | Terminator (header `0x80`) |

Words 13–15 carry one more digit of each value than the LCD shows. This is inferred, not documented: the extra digits stay consistent across repeated readings of the same target, are zero where the LCD shows zeros, and the LCD truncates rather than rounds (x = .4319 displays as .431).

How the sketch applies the extra Y digit:

| Display | Output |
|---|---|
| 0.00–9.99 | unchanged (Y has at most two decimals) |
| 10.0–99.9 | extra digit appended, e.g. `57.20` |
| 100 and above | decimal point and extra digit appended, e.g. `101.3` |

x and y always get the extra digit, e.g. `0.4319`.

### Timing

- One trigger produces exactly one frame. The meter's display updates twice during a measurement, but only one frame is sent.
- The frame finishes about 2.4 s after the trigger pulse starts. A full `M` command takes about 2.45 s.

## Serial protocol

115200 baud. Commands and replies are single lines ending in `\n` (`\r` is ignored). Replies are comma-separated, and the first field is always `OK` or `ERR`.

| Command | Action | Reply |
|---|---|---|
| `M` | Pulse the trigger (100 ms) and measure | `OK,<Y>,<x>,<y>`, e.g. `OK,57.20,0.4319,0.4150` |
| `R` | Pulse the trigger and return the raw frame | `OK,<64 hex chars>`: header and inverted segment byte for each of the 16 words |
| `?` | Check the Arduino is there | `OK,CS100` |

| Error | Meaning |
|---|---|
| `ERR,TIMEOUT` | No clock within 5 s of the trigger |
| `ERR,FRAME,<bits>` | The clock stopped before all 256 bits arrived |
| `ERR,DIGIT,<64 hex chars>` | A segment pattern was not a digit (for example an error code on the LCD); the raw frame is included |
| `ERR,CMD` | Unknown command |

The sketch prints `READY` at startup, but the Nano does not reset when MATLAB opens the port, so do not wait for it. Use `?` as the handshake.

Frames caused by pressing the meter's own measure button are ignored; only a command produces a reply.

Tunable constants at the top of `cs100_serial.ino`:

| Constant | Default | Meaning |
|---|---|---|
| `TRIGGER_PULSE_MS` | 100 | Trigger pulse length |
| `START_TIMEOUT_MS` | 5000 | Time allowed from trigger to first clock edge |
| `QUIET_TIMEOUT_US` | 100000 | Clock gap that ends an incomplete frame |

## MATLAB

```matlab
cs100_read          % prints Y, x, y and leaves Yxy = [Y x y] in the workspace
```

Set `port` at the top of `cs100_read.m` if the Nano appears under a different name (`serialportlist("all")` lists them). The script releases the port on both success and failure.

Only one program can hold the port: close the Arduino IDE Serial Monitor before using MATLAB.

## Building and uploading

From the Arduino IDE: board **Arduino Nano**, processor **ATmega328P**.

From the command line, using the copy of `arduino-cli` bundled with the IDE:

```sh
CLI="/Applications/Arduino IDE.app/Contents/Resources/app/lib/backend/resources/arduino-cli"
"$CLI" --config-file ~/.arduinoIDE/arduino-cli.yaml compile \
  --fqbn arduino:avr:nano:cpu=atmega328 --upload \
  -p /dev/cu.wchusbserial1140 .
```

## Troubleshooting

- **`ERR,TIMEOUT`:** the meter sent no clock. Check that the meter is on, then the trigger wire (D3 to pin 11), then the clock (D2 to pin 10) and ground. A trigger output accidentally connected to ground gives exactly this error.
- **Continuity between an output pin and GND:** on a powered Nano, an output held low reads as a short. Unplug USB and the meter cable before measuring resistance.
- **Port not available in MATLAB:** something else (usually the IDE Serial Monitor) has it open.
- **`ERR,DIGIT`:** decode the included raw frame with the tables above.

## Open questions

- Words 5 and 9: what the extra segment and decimal point bits mean.
- Y ≥ 1000: not yet seen, so how the meter frames four-digit values is unknown.
- Whether the extra Y digit above 100 is really the tenths digit (assumed by analogy with 10–99.9).
- Which edge of the trigger pulse starts the measurement, and why the display updates twice.

## References

- [Minolta LS-100/LS-110 Communication Manual](https://assets.tequipment.net/assets/1/26/Documents/Minolta/LS_110/ls_110_doc_6.pdf): connector pinout, schematic, measure request behaviour.
- [Konica Minolta CS-100A Instruction Manual](https://sensing.konicaminolta.us/wp-content/uploads/cs-100a_instruction_eng-gnx9h209r9.pdf): specifications; no data-output format.
