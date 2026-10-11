#include "pi1551_sound.h"
#include <cstdio>
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"FAIL sound:%d %s\n",__LINE__,#x); return 1; } } while(0)
int main() {
    Pi1551HeadSound sound;
    CHECK(!sound.Trigger(false,true,500,1200)); CHECK(sound.remainingUs == 0);
    CHECK(sound.Trigger(true,false,500,1200)); CHECK(sound.remainingUs == 0); // one sample request, no GPIO oscillation
    CHECK(!sound.Trigger(true,true,100,1000)); CHECK(sound.remainingUs == 100000);
    unsigned edges = 0; bool last = false;
    for (unsigned us = 0; us < 100000; ++us) { sound.Step(true,true,1000); if (sound.output != last) ++edges; last = sound.output; }
    CHECK(edges == 200); CHECK(!sound.output); CHECK(sound.remainingUs == 0);
    sound.Trigger(true,true,250,2000); sound.Step(false,true,2000); CHECK(!sound.output); CHECK(sound.remainingUs == 0);
    sound.Trigger(true,true,250,2000); sound.Step(true,false,2000); CHECK(!sound.output); CHECK(sound.remainingUs == 0);
    CHECK(!sound.Trigger(true,true,0,1200)); CHECK(sound.remainingUs == 0);
    puts("PASS production head sound: sample selection, buzz duration/frequency, mute and mode switch"); return 0;
}
