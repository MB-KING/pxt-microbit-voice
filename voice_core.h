#ifndef MICROBIT_VOICE_CORE_H
#define MICROBIT_VOICE_CORE_H
#include <stdint.h>
namespace voicecore {
template<int N> class Ring {
    uint8_t bytes[N];
    int head = 0, used = 0;
public:
    int size() const { return used; }
    void clear() { head = used = 0; }
    void push(uint8_t value) {
        if (used == N) { head = (head + 1) % N; --used; }
        bytes[(head + used) % N] = value;
        ++used;
    }
    uint8_t pop() {
        if (!used) return 128;
        uint8_t value = bytes[head];
        head = (head + 1) % N;
        --used;
        return value;
    }
};
// Four-bit unsigned PCM: midpoint decodes to 128, so silence has no DC offset.
inline uint8_t encode(uint8_t value) {
    int q = (int(value) + 8) / 16;
    return q > 15 ? 15 : q;
}
inline uint8_t decode(uint8_t value) { return (value & 15) * 16; }
inline uint8_t pack(uint8_t a, uint8_t b) { return encode(a) | (encode(b) << 4); }
}
#endif
