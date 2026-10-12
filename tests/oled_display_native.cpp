#include "SSD1306.h"
#include "dmRotary.h"
extern "C" {
#include "xga_font_data.h"
}
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
    DisplayProbe(LCD_MODEL model = LCD_1306_128x64) : SSD1306(1,0x3c,128,64,0,model) {}
    unsigned char* Pixels() { return frame; }
};
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"FAIL display:%d %s\n",__LINE__,#x); return 1; } } while(0)
int main() {
    for (rotary_result_t result : {NoChange,ButtonDown,ButtonUp,RotatePositive,RotateNegative}) {
        CHECK(RotaryDirection(result,false) == result);
        CHECK(RotaryDirection(RotaryDirection(result,true),true) == result);
    }
    CHECK(RotaryDirection(RotatePositive,true) == RotateNegative);
    CHECK(RotaryDirection(ButtonDown,true) == ButtonDown);
    for (LCD_MODEL model : {LCD_1306_128x64, LCD_1106_128x64}) {
        commands.clear();
        DisplayProbe panel(model);
        bool pageMode = false;
        for (size_t i = 0; i + 1 < commands.size(); ++i)
            if (commands[i] == 0x20 && commands[i+1] == 0x02) pageMode = true;
        CHECK(pageMode == (model == LCD_1306_128x64));
        for (unsigned value : {0u,10u,127u,255u}) {
            commands.clear(); panel.SetContrast(value);
            CHECK(commands.size() == 2); CHECK(commands[0] == 0x81); CHECK(commands[1] == value);
            CHECK(panel.GetContrast() == value); // no VCOMH (0xDB) or other electrical commands
        }
    }
    DisplayProbe d; oled::Frame f = {};
    for (unsigned y = 0; y < 8; ++y) { memset(f.rows[y],' ',16); f.rows[y][1] = 'A'+y; }
    transfers = 0; d.PlotMenu(f,false); CHECK(transfers == 0);
    unsigned char normal[1024]; memcpy(normal,d.Pixels(),1024);
    for (unsigned x = 0; x < 8; ++x) {
        unsigned char expected = 0;
        for (unsigned y = 0; y < 8; ++y)
            if (avpriv_cga_font['A'*8+y] & (0x80 >> x)) expected |= 1u << y;
        CHECK(normal[8+x] == expected);
    }
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
