#include "pxt.h"
#if MICROBIT_CODAL
#include "MicroBit.h"
#include "MemorySource.h"
#include "voice_core.h"
#endif
using namespace pxt;

namespace voice {
#if MICROBIT_CODAL
using namespace codal;

// 4 kHz, 4-bit. 29 audio bytes per 32-byte radio packet is 58 samples,
// about 69 packets/second. The native radio can carry that; the MakeCode
// JavaScript send loop cannot, and it also truncates buffers at 19 bytes.
static const int PACKET_BYTES = 32;
static const int AUDIO_BYTES = 29;
static const int SAMPLES_PER_PACKET = AUDIO_BYTES * 2;
static const int PLAY_CHUNK = 128;

static bool ready = false;
static bool transmitting = false;
static bool stopPending = false;
static bool ended = false;
static uint8_t sequence = 0;
static int deviceId = 1;
static int peer = -1;
static int expected = -1;
static uint32_t lastPacket = 0;
static voicecore::Ring<4096> captured;
static voicecore::Ring<8192> received;
static SplitterChannel *micChannel;
static MemorySource *player;

// The splitter downstream of StreamNormalizer emits 8-bit signed samples
// (silence is 0). The codec and speaker path expect unsigned samples
// (silence is 128).
static uint8_t toUnsigned(uint8_t raw) {
    int sample = (int)(int8_t)raw * 6;
    if (sample > 127) sample = 127;
    if (sample < -128) sample = -128;
    return (uint8_t)(sample + 128);
}

class Capture : public DataSink {
public:
    virtual int pullRequest() override {
        if (!transmitting) return DEVICE_OK;
        ManagedBuffer b = micChannel->pull();
        __disable_irq();
        for (int i = 0; i < b.length(); ++i) captured.push(toUnsigned(b[i]));
        __enable_irq();
        return DEVICE_OK;
    }
};
static Capture *capture;

static uint8_t popCaptured() {
    __disable_irq();
    uint8_t value = captured.pop();
    __enable_irq();
    return value;
}

static int capturedSize() {
    __disable_irq();
    int count = captured.size();
    __enable_irq();
    return count;
}

static void pushReceived(uint8_t value) {
    __disable_irq();
    received.push(value);
    __enable_irq();
}

static uint8_t popReceived() {
    __disable_irq();
    uint8_t value = received.pop();
    __enable_irq();
    return value;
}

static int receivedSize() {
    __disable_irq();
    int count = received.size();
    __enable_irq();
    return count;
}

static void sendRadio(const uint8_t *data, int length) {
    uBit.radio.enable();
    uBit.radio.datagram.send((uint8_t *)data, length);
}

static bool sendDataPacket(bool pad) {
    int have = capturedSize();
    if (have < 2) return false;
    if (!pad && have < SAMPLES_PER_PACKET) return false;
    uint8_t packet[PACKET_BYTES];
    packet[0] = 0xC7;
    packet[1] = (uint8_t)deviceId;
    packet[2] = sequence++;
    for (int i = 0; i < AUDIO_BYTES; ++i) {
        uint8_t a = capturedSize() ? popCaptured() : 128;
        uint8_t b = capturedSize() ? popCaptured() : 128;
        packet[3 + i] = voicecore::pack(a, b);
    }
    sendRadio(packet, PACKET_BYTES);
    return true;
}

static void sendEndPacket() {
    uint8_t packet[3] = {0xC8, (uint8_t)deviceId, 0};
    sendRadio(packet, 3);
}

static void handlePacket(PacketBuffer packet) {
    if (transmitting || packet.length() < 3) return;
    const uint8_t *p = packet.getBytes();
    if (p[0] != 0xC7 && p[0] != 0xC8) return;
    if (p[1] == (uint8_t)deviceId) return;
    uint32_t now = system_timer_current_time();
    if (peer != p[1]) {
        if (peer != -1 && now - lastPacket < 250) return;
        peer = p[1];
        expected = -1;
        __disable_irq();
        received.clear();
        __enable_irq();
    } else if (now - lastPacket > 250) {
        expected = -1;
        __disable_irq();
        received.clear();
        __enable_irq();
    }
    if (p[0] == 0xC8) {
        ended = true;
        lastPacket = now;
        return;
    }
    if (packet.length() != PACKET_BYTES) return;
    if (expected >= 0) {
        int gap = (p[2] - expected + 256) & 255;
        if (gap >= 128) return;
        if (gap > 8) {
            __disable_irq();
            received.clear();
            __enable_irq();
        } else {
            for (int i = 0; i < gap * SAMPLES_PER_PACKET; ++i) pushReceived(128);
        }
    }
    expected = (p[2] + 1) & 255;
    ended = false;
    lastPacket = now;
    for (int i = 3; i < PACKET_BYTES; ++i) {
        pushReceived(voicecore::decode(p[i]));
        pushReceived(voicecore::decode(p[i] >> 4));
    }
}

static void playChunk() {
    int available = receivedSize();
    if (!available) return;
    if (available < PLAY_CHUNK && !ended &&
        system_timer_current_time() - lastPacket < 60) return;
    int count = available < PLAY_CHUNK ? available : PLAY_CHUNK;
    ManagedBuffer audio(count);
    for (int i = 0; i < count; ++i) audio[i] = popReceived();
    if (!receivedSize()) ended = false;
    player->play(audio);
}

static void worker() {
    while (true) {
        if (transmitting || stopPending) {
            // Drop anything queued while the speaker is muted so the
            // four-packet radio queue cannot fill up.
            for (int i = 0; i < 8; ++i) {
                if (uBit.radio.datagram.recv().length() == 0) break;
            }
            if (!sendDataPacket(stopPending && !transmitting)) {
                if (stopPending && capturedSize() < 2) {
                    sendEndPacket();
                    stopPending = false;
                }
                fiber_sleep(1);
            }
        } else {
            for (int i = 0; i < 8; ++i) {
                PacketBuffer packet = uBit.radio.datagram.recv();
                if (packet.length() == 0) break;
                handlePacket(packet);
            }
            if (receivedSize()) playChunk();
            else fiber_sleep(1);
        }
    }
}

static void ensureReady() {
    if (ready) return;
    MicroBitAudio::requestActivation();
    uBit.radio.enable();
    micChannel = uBit.audio.splitter->createChannel();
    uBit.audio.mic->setSampleRate(8000);
    micChannel->requestSampleDropRate(2);
    capture = new Capture();
    micChannel->connect(*capture);
    micChannel->dataWanted(DATASTREAM_NOT_WANTED);
    player = new MemorySource();
    player->setFormat(DATASTREAM_FORMAT_8BIT_UNSIGNED);
    player->setBufferSize(PLAY_CHUNK);
    MixerChannel *output = uBit.audio.mixer.addChannel(*player, 4000);
    output->setVolume(100.0);
    uBit.audio.setVolume(255);
    uBit.audio.setSpeakerEnabled(true);
    uBit.audio.setPinEnabled(false);
    ready = true;
    create_fiber(worker);
}
#endif

//%
void setup() {
#if MICROBIT_CODAL
    ensureReady();
#else
    target_panic(PANIC_VARIANT_NOT_SUPPORTED);
#endif
}

//%
void setDevice(int id) {
#if MICROBIT_CODAL
    if (id < 0) id = 0;
    if (id > 255) id = 255;
    deviceId = id;
#else
    target_panic(PANIC_VARIANT_NOT_SUPPORTED);
#endif
}

//%
void startCapture() {
#if MICROBIT_CODAL
    ensureReady();
    __disable_irq();
    captured.clear();
    received.clear();
    __enable_irq();
    peer = expected = -1;
    ended = false;
    stopPending = false;
    transmitting = true;
    uBit.audio.setSpeakerEnabled(false);
    micChannel->dataWanted(DATASTREAM_WANTED);
    uBit.audio.activateMic();
#else
    target_panic(PANIC_VARIANT_NOT_SUPPORTED);
#endif
}

//%
void stopCapture() {
#if MICROBIT_CODAL
    if (!ready) return;
    stopPending = true;
    transmitting = false;
    micChannel->dataWanted(DATASTREAM_NOT_WANTED);
    uBit.audio.setSpeakerEnabled(true);
#else
    target_panic(PANIC_VARIANT_NOT_SUPPORTED);
#endif
}

}
