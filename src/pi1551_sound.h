#ifndef PI1551_SOUND_H
#define PI1551_SOUND_H
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
