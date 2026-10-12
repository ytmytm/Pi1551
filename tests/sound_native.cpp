#include "pi1551_sound.h"
#include <cstdio>
#include <vector>
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
    std::vector<unsigned> preview(Pi1551SoundPreview::Frames);
    const unsigned char sample[] = {0,128,255};
    Pi1551SoundPreview::Fill(preview.data(),true,true,1000,1000,sample,3);
    unsigned crossings = 0;
    for (unsigned i = 1; i < preview.size(); ++i) crossings += preview[i] != preview[i-1];
    CHECK(crossings == 1999); // one second, 1 kHz, independent of UI poll cadence
    Pi1551SoundPreview::Fill(preview.data(),true,true,250,2000,sample,3);
    unsigned audible = 0; for (unsigned v : preview) if (v != 128) ++audible;
    CHECK(audible == (Pi1551SoundPreview::Rate * 250 + 999) / 1000);
    Pi1551SoundPreview::Fill(preview.data(),true,false,0,0,sample,3);
    for (unsigned i = 0; i < preview.size(); ++i) CHECK(preview[i] == sample[i%3]);
    Pi1551SoundPreview::Fill(preview.data(),false,true,1000,2000,sample,3);
    for (unsigned v : preview) CHECK(v == 128);
    Pi1551SoundPreview::Fill(preview.data(),true,true,0,2000,sample,3);
    for (unsigned v : preview) CHECK(v == 128);
    puts("PASS one-second sound preview: frequency, duration, sample loop and mute");
    puts("PASS production head sound: sample selection, buzz duration/frequency, mute and mode switch"); return 0;
}
