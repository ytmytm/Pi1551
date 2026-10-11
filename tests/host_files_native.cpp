#include "commands_base.h"
#include <string>
#include <fstream>
#include <sstream>
extern void NativeFatFSFail(const std::string& operation);
struct Access : Commands_Base { using Commands_Base::Channel; };
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"FAIL host files:%d %s\n",__LINE__,#x); return 1; } } while(0)
int main(int argc,char** argv) {
    CHECK(argc == 2);
    Access::Channel c = {};
    CHECK(f_open(&c.file,argv[1],FA_WRITE|FA_CREATE_NEW) == FR_OK);
    c.open = c.writing = true; c.cursor = 9; memcpy(c.buffer,"abcdefghi",9);
    NativeFatFSFail("write"); CHECK(!c.Finish()); CHECK(c.open); CHECK(c.cursor == 9);
    NativeFatFSFail("partial"); CHECK(!c.Finish()); CHECK(c.open); CHECK(c.cursor == 5);
    NativeFatFSFail("sync"); CHECK(!c.Finish()); CHECK(c.open); CHECK(c.cursor == 0);
    CHECK(c.Finish()); CHECK(!c.open);
    std::ifstream f(argv[1]); std::ostringstream s; s << f.rdbuf(); CHECK(s.str() == "abcdefghi");
    CHECK(c.Finish());
    Access::Channel virtualFile = {};
    virtualFile.open = true; virtualFile.imageFile = true;
    CHECK(virtualFile.Finish()); CHECK(!virtualFile.open); // no FatFS handle exists
    puts("PASS actual host write buffer: partial write retry, sync failure and close before reboot");
    return 0;
}
