#ifndef PI1551_SOUND_H
#define PI1551_SOUND_H
// Existing PWM setup: 19.2 MHz oscillator / 2, range 436 (GPIO13 PWM1).
// A preview occupies one second, preserving short buzzer duration and mute.
struct Pi1551SoundPreview {
    static const unsigned Rate = 9600000 / 436;
    static const unsigned Frames = Rate;
    static void Fill(unsigned* data, bool enabled, bool buzzer, unsigned durationMs,
                     unsigned frequency, const unsigned char* sample, unsigned sampleSize) {
        for (unsigned i = 0; i < Frames; ++i) {
            unsigned value = 128;
            if (enabled && buzzer && i * 1000u / Rate < durationMs)
                value = ((i * frequency * 2u / Rate) & 1) ? 255 : 0;
            else if (enabled && !buzzer && sample && sampleSize) value = sample[i % sampleSize];
            data[i] = value;
        }
    }
};
// Head-step audio timing shared by the real-time loop and native tests.
struct Pi1551HeadSound {
    unsigned remainingUs = 0, phaseUs = 0;
    bool output = false;
    bool Trigger(bool enabled, bool buzzer, unsigned durationMs, unsigned frequency) {
        remainingUs = phaseUs = 0; output = false;
        if (!enabled) return false;
        if (!buzzer) return true; // request one autonomous PWM/DMA sample
        remainingUs = durationMs * 1000;
        phaseUs = HalfPeriod(frequency);
        return false;
    }
    static unsigned HalfPeriod(unsigned frequency) { return frequency ? 500000 / frequency : 500; }
    void Step(bool enabled, bool buzzer, unsigned frequency) {
        if (!enabled || !buzzer || !remainingUs) { remainingUs = phaseUs = 0; output = false; return; }
        if (!--remainingUs) { output = false; return; }
        if (!phaseUs || !--phaseUs) { phaseUs = HalfPeriod(frequency); output = !output; }
    }
};
#endif
