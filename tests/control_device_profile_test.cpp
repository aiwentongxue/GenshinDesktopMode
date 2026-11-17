#include "runtime_image_fixture.h"
#include "../genshin/control_device_profile.h"
#include <iostream>
static void Check(const char *path,bool global) {
    Image image(path);
    for(auto &entry:ControlDeviceProfile::entries) {
        uintptr_t address=global?entry.global:entry.cn;
        auto code=image.read(address,96,false);
        assert(code&&!memcmp(code,global?entry.globalCode:entry.cnCode,96));
        auto mutated=const_cast<uint8_t *>(code);mutated[64]^=1;
        assert(memcmp(code,global?entry.globalCode:entry.cnCode,96));
        mutated[64]^=1;
    }
    std::cout<<(global?"global":"CN")<<": all native settings roles match actual executable; modified bodies rejected\n";
}
int main(int argc,char **argv) {
    assert(argc==3);
    static_assert(ControlDeviceProfile::CNRow!=ControlDeviceProfile::GlobalRow);
    static_assert(ControlDeviceProfile::CNOptions!=ControlDeviceProfile::GlobalOptions);
    Check(argv[1],false);Check(argv[2],true);
}
