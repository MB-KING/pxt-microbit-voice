#include "pxt.h"
#if MICROBIT_CODAL
#include "MemorySource.h"
#include "voice_core.h"
#endif
using namespace pxt;

namespace voice {
#if MICROBIT_CODAL
using namespace codal;
static bool ready = false;
static bool transmitting = false;
static bool ended = false;
static uint8_t sequence = 0;
static int peer = -1;
static int expected = -1;
static uint32_t lastPacket = 0;
static voicecore::Ring<512> captured;
static voicecore::Ring<2048> received;
static SplitterChannel *micChannel;
static MemorySource *player;

class Capture : public DataSink {
public:
    virtual int pullRequest() override {
        if (!transmitting) return DEVICE_OK;
        ManagedBuffer b = micChannel->pull();
        for (int i = 0; i < b.length(); ++i) captured.push(b[i]);
        return DEVICE_OK;
    }
};
static Capture *capture;

static void ensureReady() {
    if (ready) return;
    MicroBitAudio::requestActivation();
    micChannel = uBit.audio.splitter->createChannel();
    // Capture at 8 kHz then average each pair, yielding a 4 kHz voice stream.
    uBit.audio.mic->setSampleRate(8000);
    micChannel->requestSampleDropRate(2);
    capture = new Capture();
    micChannel->connect(*capture);
    micChannel->dataWanted(DATASTREAM_NOT_WANTED);
    player = new MemorySource();
    player->setFormat(DATASTREAM_FORMAT_8BIT_UNSIGNED);
    player->setBufferSize(128);
    MixerChannel *output = uBit.audio.mixer.addChannel(*player, 4000);
    output->setVolume(75.0);
    uBit.audio.setVolume(180);
    uBit.audio.setSpeakerEnabled(true);
    uBit.audio.setPinEnabled(false);
    ready = true;
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
void startCapture() {
#if MICROBIT_CODAL
    ensureReady();
    captured.clear(); received.clear();
    peer = expected = -1;
    // Do not reset sequence: receiver can distinguish adjacent talk bursts.
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
    transmitting = false;
    micChannel->dataWanted(DATASTREAM_NOT_WANTED);
    uBit.audio.deactivateMic();
    uBit.audio.setSpeakerEnabled(true);
#endif
}

//%
Buffer nextPacket(int sender) {
#if MICROBIT_CODAL
    ensureReady();
    int count = captured.size();
    if (!count || (transmitting && count < 32)) return mkBuffer(NULL, 0);
    Buffer b = mkBuffer(NULL, 19);
    b->data[0] = 0xC7;
    b->data[1] = sender;
    b->data[2] = sequence++;
    for (int i = 0; i < 16; ++i) {
        uint8_t a = captured.pop();
        uint8_t c = captured.pop();
        b->data[3 + i] = voicecore::pack(a, c);
    }
    return b;
#else
    target_panic(PANIC_VARIANT_NOT_SUPPORTED);
    return mkBuffer(NULL, 0);
#endif
}

//%
void receivePacket(Buffer b) {
#if MICROBIT_CODAL
    ensureReady();
    if (transmitting || !b || b->length < 3) return;
    const uint8_t *p = b->data;
    if (p[0] != 0xC7 && p[0] != 0xC8) return;
    uint32_t now = system_timer_current_time();
    if (peer != p[1]) {
        if (peer != -1 && now - lastPacket < 250) return;
        peer = p[1]; expected = -1; received.clear();
    } else if (now - lastPacket > 250) {
        expected = -1; received.clear();
    }
    if (p[0] == 0xC8) { ended = true; lastPacket = now; return; }
    if (b->length != 19) return;
    if (expected >= 0) {
        int gap = (p[2] - expected + 256) & 255;
        if (gap >= 128) return; // Duplicate or old packet.
        if (gap > 8) received.clear();
        else for (int i = 0; i < gap * 32; ++i) received.push(128);
    }
    expected = (p[2] + 1) & 255;
    ended = false;
    lastPacket = now;
    for (int i = 3; i < 19; ++i) {
        received.push(voicecore::decode(p[i]));
        received.push(voicecore::decode(p[i] >> 4));
    }
#endif
}

//% promise
void playPending() {
#if MICROBIT_CODAL
    ensureReady();
    if (transmitting || !received.size()) { fiber_sleep(1); return; }
    // Buffer 32 ms; flush the tail on end-of-burst or a reception timeout.
    if (received.size() < 128 && !ended &&
        system_timer_current_time() - lastPacket < 60) {
        fiber_sleep(1); return;
    }
    int count = received.size() < 128 ? received.size() : 128;
    ManagedBuffer audio(count);
    for (int i = 0; i < count; ++i) audio[i] = received.pop();
    // Blocking until the mixer has copied the buffer, preserving its lifetime.
    player->play(audio);
#else
    target_panic(PANIC_VARIANT_NOT_SUPPORTED);
#endif
}
}
