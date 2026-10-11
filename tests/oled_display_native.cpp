#include "SSD1306.h"
#include <cstring>
#include <cstdio>
#include <vector>
unsigned char* CBMFont = 0;
static std::vector<unsigned char> commands;
static unsigned transfers = 0;
extern "C" void RPI_I2CInit(int,int) {}
extern "C" int RPI_I2CWrite(int,unsigned char,void* data,unsigned count) {
    ++transfers; unsigned char* bytes = (unsigned char*)data;
    if (count == 2 && bytes[0] == 0) commands.push_back(bytes[1]);
    return count;
}
struct DisplayProbe : SSD1306 {
    DisplayProbe() : SSD1306(1,0x3c,128,64,0,LCD_1306_128x64) {}
    unsigned char* Pixels() { return frame; }
};
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"FAIL display:%d %s\n",__LINE__,#x); return 1; } } while(0)
int main() {
    DisplayProbe d; oled::Frame f = {};
    for (unsigned y = 0; y < 8; ++y) { memset(f.rows[y],' ',16); f.rows[y][1] = 'A'+y; }
    transfers = 0; d.PlotMenu(f,false); CHECK(transfers == 0);
    unsigned char normal[1024]; memcpy(normal,d.Pixels(),1024);
    for (unsigned y = 0; y < 8; ++y) {
        bool set = false; for (unsigned x = 8; x < 16; ++x) set |= normal[y*128+x] != 0;
        CHECK(set); // all eight lines occupy their own 8-pixel page
    }
    f.inverse[4] = true; d.PlotMenu(f,false);
    for (unsigned x = 0; x < 128; ++x) CHECK(d.Pixels()[4*128+x] == (unsigned char)~normal[4*128+x]);
    f.active[4] = true; d.PlotMenu(f,false); bool triangle = false;
    for (unsigned x = 0; x < 8; ++x) triangle |= d.Pixels()[4*128+x] != 0xff;
    CHECK(triangle); CHECK(f.inverse[4]);
    commands.clear(); d.SetOrientation(1); CHECK(commands.size() == 2); CHECK(commands[0] == 0xA0); CHECK(commands[1] == 0xC0);
    d.SetOrientation(1); CHECK(commands.size() == 2);
    d.SetOrientation(0); CHECK(commands[2] == 0xA1); CHECK(commands[3] == 0xC8);
    transfers = 0; d.RefreshScreen(); CHECK(transfers > 0);
    puts("PASS actual OLED raster: eight rows, inversion, triangle, orientation and I2C effects");
    return 0;
}
