# Experimental micro:bit V2 voice extension

for PXT/microbit

**Prototype source, not hardware-validated.** Native firmware compilation has not
been verified. Do not assume this will work simply by pasting the demo into a
normal MakeCode project. This package must first be imported as a native extension.
No HEX firmware is included.

Two V2 boards use microphone capture at 8 kHz, pair averaging to 4 kHz, and
4-bit unsigned PCM. Each 19-byte radio packet holds 32 samples, requiring about
125 packets per second. This deliberately sacrifices audio bandwidth and resolution.
Actual throughput, sample timing, microphone gain, jitter and intelligibility must
be tested on the hardware. Capture, radio transmit/receive and playback run in the
native extension, because MakeCode's JavaScript radio loop cannot sustain this rate.

## Install

1. Create a public GitHub repository, such as `pxt-microbit-voice`.
2. Upload the **contents of this extension folder to the repository root**:
   `pxt.json`, `voice.ts`, `voice.cpp`, `voice_core.h`, `README.md`.
3. In MakeCode create a new project. Open Extensions and paste the GitHub repository URL.
4. Add the Radio extension if it is not already included.
5. Paste `demo/main.ts` from the downloaded package into the project's JavaScript tab.
6. Set `DEVICE_ID` to 1 for the first board, download it. Set it to 2 for the second.
7. Hold A on one board and speak; release A before the other person speaks.
   Both boards must use radio group 77. Start testing with boards about one metre apart.

The simulator stubs do not capture or transmit voice. Testing needs physical V2
boards. V1 will panic as unsupported. Do not add the standard audio-recording
extension or other microphone sample-rate controls to this project.

## First test and reporting

- First confirm the project compiles for V2. If compilation fails, keep the full
  compiler error; do not substitute sound-level readings for voice samples.
- Check that holding A turns the microphone activity indicator on.
- Test a short spoken sentence, alternating boards, then test a longer burst.
- Record whether there is intelligible speech, silence, severe distortion or breaks.
- Group numbers are not encryption or authentication; anyone on the group may
  receive packets. This protocol is for two participants taking turns.

## What was verified

The portable C++ test checks queue overflow/rollover, silence padding, PCM midpoint
and clipping. It passed using g++ with `-Wall -Wextra -Werror`.
The native CODAL integration and radio/audio performance have NOT been compiled or
hardware-tested. Source APIs were inspected in Microsoft's pxt-microbit
`libs/audio-recording/recording.cpp` and Lancaster University's CODAL MicroBitAudio,
StreamSplitter and MemorySource sources. These upstream APIs may differ from the
version pinned by your MakeCode editor.

## Reference sources

https://github.com/microsoft/pxt-microbit/blob/master/libs/audio-recording/recording.cpp
https://github.com/lancaster-university/codal-microbit-v2/blob/master/inc/MicroBitAudio.h
https://github.com/lancaster-university/codal-core/blob/master/inc/streams/StreamSplitter.h
https://github.com/lancaster-university/codal-core/blob/master/inc/streams/MemorySource.h
https://makecode.com/extensions/getting-started

License: MIT (original prototype code).
