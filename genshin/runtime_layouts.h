#pragma once
#include "runtime_layout_global_modern.h"

enum class GenshinRuntimeLayout { Unsupported, MainlandInput, GlobalInput, ModernInput, GlobalModernInput };
struct GenshinManagedOffsets {
    size_t ownerStatic,uiStatic,ownerEventSystem,ownerModule,ownerHybrid,ownerGameInput,ownerInputMode;
    size_t inputPlayer,inputManager,playerControllers,uiLayout;
    size_t wheelInputMode,wheelMono,wheelPosition,wheelSelected,monoCursor;
};
static constexpr GenshinManagedOffsets GenshinMainlandOffsets={
    0x8bf0,0x8c50,72,240,120,184,340,64,208,24,832,0x288,0x258,0x290,0x29c,248};
static constexpr GenshinManagedOffsets GenshinGlobalOffsets={
    0x8bc0,0x8b40,24,48,240,208,304,32,168,32,800,0x28c,0x230,0x290,0x298,248};
static constexpr GenshinManagedOffsets GenshinModernOffsets={
    0x8da0,0x8d90,184,288,240,56,304,144,184,32,912,636,568,644,632,248};
static constexpr GenshinManagedOffsets GenshinGlobalModernOffsets={
    0x8e60,0x8c00,80,184,72,192,368,72,80,24,828,672,552,688,652,248};

static constexpr unsigned GenshinMethodCodeWord(GenshinRuntimeLayout layout) {
    return layout==GenshinRuntimeLayout::ModernInput?0:1;
}
static constexpr unsigned GenshinMethodFlagsOffset(GenshinRuntimeLayout layout) {
    return layout==GenshinRuntimeLayout::ModernInput?0x2c:0x28;
}
static constexpr bool GenshinModernKeyboardPolicy(GenshinRuntimeLayout layout) {
    return layout==GenshinRuntimeLayout::ModernInput||layout==GenshinRuntimeLayout::GlobalModernInput;
}

template<class Reader>
static GenshinRuntimeLayout SelectGenshinRuntimeLayout(Reader reader,uintptr_t &mismatch) {
    if(MatchesGenshinRuntimeLayout(GenshinModernRuntimePatterns,
            sizeof(GenshinModernRuntimePatterns)/sizeof(GenshinModernRuntimePatterns[0]),
            reader,mismatch,0x115ba0c08)) return GenshinRuntimeLayout::ModernInput;
    if(MatchesGenshinRuntimeLayout(GenshinGlobalModernRuntimePatterns,
            sizeof(GenshinGlobalModernRuntimePatterns)/sizeof(GenshinGlobalModernRuntimePatterns[0]),
            reader,mismatch,0x115d94488)) return GenshinRuntimeLayout::GlobalModernInput;
    if(MatchesGenshinRuntimeLayout(GenshinRuntimePatterns,
            sizeof(GenshinRuntimePatterns)/sizeof(GenshinRuntimePatterns[0]),reader,mismatch))
        return GenshinRuntimeLayout::MainlandInput;
    if(MatchesGenshinRuntimeLayout(GenshinGlobalRuntimePatterns,
            sizeof(GenshinGlobalRuntimePatterns)/sizeof(GenshinGlobalRuntimePatterns[0]),
            reader,mismatch,0x11551c120)) return GenshinRuntimeLayout::GlobalInput;
    return GenshinRuntimeLayout::Unsupported;
}
static uintptr_t ResolveGenshinAddress(GenshinRuntimeLayout layout,uintptr_t logical) {
    if(layout==GenshinRuntimeLayout::MainlandInput) return logical;
    if(layout==GenshinRuntimeLayout::GlobalInput)
        for(auto binding:GenshinGlobalAddressBindings)
            if(binding.logical==logical) return binding.actual;
    if(layout==GenshinRuntimeLayout::ModernInput)
        for(auto binding:GenshinModernAddressBindings)
            if(binding.logical==logical) return binding.actual;
    if(layout==GenshinRuntimeLayout::GlobalModernInput)
        for(auto binding:GenshinGlobalModernAddressBindings)
            if(binding.logical==logical) return binding.actual;
    return 0; // Never fall back to mainland addresses for an unknown binding.
}
static const GenshinRuntimePattern *GenshinExpectedPattern(GenshinRuntimeLayout layout,uintptr_t logical) {
    uintptr_t actual=ResolveGenshinAddress(layout,logical);
    if(layout==GenshinRuntimeLayout::MainlandInput) {
        for(const auto &p:GenshinRuntimePatterns) if(p.address==actual) return &p;
    } else if(layout==GenshinRuntimeLayout::GlobalInput) {
        for(const auto &p:GenshinGlobalRuntimePatterns) if(p.address==actual) return &p;
    } else if(layout==GenshinRuntimeLayout::ModernInput) {
        for(const auto &p:GenshinModernRuntimePatterns) if(p.address==actual) return &p;
    } else if(layout==GenshinRuntimeLayout::GlobalModernInput) {
        for(const auto &p:GenshinGlobalModernRuntimePatterns) if(p.address==actual) return &p;
    }
    return nullptr;
}
