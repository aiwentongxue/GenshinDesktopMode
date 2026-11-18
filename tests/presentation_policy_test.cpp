#include "../genshin/presentation_policy.h"
#include <cassert>
#include <iostream>

int main() {
    // Default and explicit negatives leave the game's drawable queue alone.
    assert(!ReducedDrawableQueueRequested(nullptr));
    assert(!ReducedDrawableQueueRequested(""));
    assert(!ReducedDrawableQueueRequested("0"));
    assert(!ReducedDrawableQueueRequested("false"));
    assert(!ReducedDrawableQueueRequested("no"));
    assert(!ReducedDrawableQueueRequested("2"));
    // Only an explicit opt-in may shorten the queue.
    assert(ReducedDrawableQueueRequested("1"));
    assert(ReducedDrawableQueueRequested("true"));
    assert(ReducedDrawableQueueRequested("TRUE"));
    assert(ReducedDrawableQueueRequested("yes"));
    std::cout << "presentation_policy_test passed\n";
    return 0;
}
