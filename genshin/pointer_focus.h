#pragma once

namespace DesktopPointerFocus {
enum class Window { none, key, main };

// A packet routed to a tooltip or other auxiliary window does not change
// which window has keyboard focus. With no key window, require positive
// evidence that AppKit addressed this packet to the visible main game.
inline Window owner(bool active, bool nativeUI, bool hasKey, bool keyIsGame,
                    bool eventTargetsVisibleMainGame) {
    if (!active || nativeUI) return Window::none;
    if (hasKey) return keyIsGame ? Window::key : Window::none;
    return eventTargetsVisibleMainGame ? Window::main : Window::none;
}
}
