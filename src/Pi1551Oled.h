#ifndef PI1551_OLED_H
#define PI1551_OLED_H
#include "OledMenu.h"
namespace Pi1551Oled {
void Initialize();
bool Enabled();
void PollInput(); // core 0 only
void ObserveHostReset(bool asserted); // core 0: raw RESET, independent of drive policy
void Draw(); // core 0 only, including all I2C traffic
void Service(bool emulating); // core 1 only
bool TransitionPending();
bool InputPending();
void CompleteTransition(); // core 1, after leaving the real-time loop
void HostReset();
void HostClearList();
void HostSelection(const char* path);
bool HostDirectory(); // adopt cwd already changed by the host
bool HostParent(); // mounted image -> containing SD folder; SD folder -> parent
bool HostRoot();
void HostRefresh(const char* containerPath = 0);
std::string MountedPath();
void KernelError(const char* message); // detailed failure from core 1 kernel hooks
#ifdef PI1551_NATIVE_TEST
void TestInput(oled::Key key);
oled::Menu& TestMenu();
void TestShutdown();
#endif
}
// Kernel hooks: called only by core 1.
bool OledKernelMount(const char* path);
bool OledKernelUnmount();
void OledKernelReset();
void OledKernelSelectROM(unsigned index);
void OledKernelApplySettings();
oled::Status OledKernelStatus();
#endif
