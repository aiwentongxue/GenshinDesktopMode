#include "pointer_focus.h"
#include "pointer_geometry.h"
#include <cassert>

int main() {
    using namespace DesktopPointerFocus;
    // An auxiliary window received an event while the game remained key.
    assert(owner(true, false, true, true, false) == Window::key);
    // A real panel became key while a game window was still on screen.
    assert(owner(true, false, true, false, true) == Window::none);
    assert(owner(true, true, true, true, false) == Window::none);
    assert(owner(false, false, true, true, true) == Window::none);
    assert(owner(true, false, false, false, false) == Window::none);
    assert(owner(true, false, false, false, true) == Window::main);
    // The two edges can arrive at different coordinates with no intervening
    // hover packet (trackpad tap or a pointer warp).
    auto viewport = DesktopGeometry::FitViewport({91,61,1329,831},3024,1890);
    auto first = DesktopGeometry::Normalize(viewport, {200,200});
    auto second = DesktopGeometry::Normalize(viewport, {900,700});
    assert(first.x < second.x && first.y > second.y);
}
