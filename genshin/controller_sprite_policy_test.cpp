#include "controller_sprite_policy.h"
#include <cassert>
#include <initializer_list>
#include <cstring>

int main() {
    using namespace DesktopControllerUI;
    using namespace DesktopControllerSprites;

    assert(Resolve(Model::Xbox, Transport::USB) == Skin::Xbox);
    assert(Resolve(Model::DualSense, Transport::USB) == Skin::PSWhite);
    assert(Resolve(Model::DualSense, Transport::Bluetooth) == Skin::PSColored);
    assert(Resolve(Model::Unknown, Transport::Unknown) == Skin::Native);

    for (Skin skin : {Skin::Native, Skin::Xbox, Skin::PSColored, Skin::PSWhite}) {
        for (const char *key : {"UI_KeyXbox_0", "UI_KeyXbox_1", "UI_KeyPs4_00",
                                "UI_KeyPs4_1_suffix", "UI_KeyKeyboard_F", "?", ""}) {
            assert(Replacement(key, skin) == nullptr);
        }
    }

    for (auto pair : pairs) {
        const char *white = Replacement(pair.colored, Skin::PSWhite);
        assert(white && std::strcmp(white, pair.white) == 0);
        assert(Replacement(white, Skin::PSWhite) == nullptr);
        const char *colored = Replacement(white, Skin::PSColored);
        assert(colored && std::strcmp(colored, pair.colored) == 0);
        assert(Replacement(colored, Skin::PSColored) == nullptr);
    }
}
