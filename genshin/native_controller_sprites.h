#pragma once
#include "controller_glyph_profiles.h"
#include "controller_sprite_policy.h"

// Substitute exact glyph keys at the atlas loader. The game's controller
// family also selects button bindings, so leave its family setter alone.
namespace NativeControllerSprites {
using namespace SceneUIProbe;
using DesktopControllerUI::Skin;
using Loader=void *(*)(void *,void *,void *,void *);
static Loader originalLoad;
static void *loaderClass,*loaderInfo,*gameClass;
static size_t loaderOffset;
static bool installed;
static uint64_t appliedRevision;
static std::atomic<bool> refreshQueued{false};
static std::atomic<unsigned> missing{0};
static bool ASCII(void *value,char *out,size_t capacity) {
    if(!value)return false;
    int length=*(int *)((char *)value+16);
    if(length<0||(size_t)length>=capacity)return false;
    const uint16_t *chars=(const uint16_t *)((char *)value+20);
    for(int i=0;i<length;++i){if(chars[i]>127)return false;out[i]=(char)chars[i];}
    out[length]=0;return true;
}
static void *Text(const char *value) {
    return EngineFunction<void *(*)(const char *)>(0x10e9bb794)(value);
}
static void *Load(void *loader,void *key,void *category,void *method) {
    char c[48],k[64];
    if(loader&&*(void **)loader==loaderClass&&ASCII(category,c,sizeof(c))&&
       !strcmp(c,DesktopControllerSprites::category)&&ASCII(key,k,sizeof(k))){
        const char *replacement=DesktopControllerSprites::Replacement(k,DesktopControllerIdentity::skin.load());
        if(replacement){
            void *name=Text(replacement);
            void *sprite=name?originalLoad(loader,name,category,method):nullptr;
            if(sprite)return sprite;
            if(missing.fetch_add(1)<5)Log(@"controller_sprite missing=%s fallback=%s",replacement,k);
        }
    }
    return originalLoad(loader,key,category,method);
}
static bool TryInstall() {
    if(installed)return true;
    if(!IsModernRuntime()||!NativeBridge::frames.load()||!DesktopControlPreference::bootstrapReady.load())return false;
    char *owner=VerifiedInputOwner();
    void *game=owner?*(void **)(owner+GenshinOffsets().ownerGameInput):nullptr;
    if(!game||strcmp(className(*(void **)game),GameInputClassName()))return false;
    loaderOffset=IsGlobalModernRuntime()?96:72;
    void *cls=ManagedFieldClassAt(*(void **)game,loaderOffset,"AEHEPABFEBB");
    if(!cls)return false;
    const auto &entry=IsGlobalModernRuntime()?ControllerGlyphProfiles::sprite_global:ControllerGlyphProfiles::sprite_cn;
    const void *body=RuntimeImageRange(entry.address,sizeof(entry.expected),false);
    if(!body||memcmp(body,entry.expected,sizeof(entry.expected)))return false;
    void *method=ControlDevice::MethodAt(cls,(uintptr_t)body,"Sprite",{"String","String"});
    if(!method||!NativeCameraPointer::RegisterOnlyPrologue(body))return false;
    originalLoad=(Loader)NativeCameraPointer::Trampoline((void *)body);if(!originalLoad)return false;
    loaderClass=cls;loaderInfo=method;gameClass=*(void **)game;
    uint8_t jump[16];NativeBridge::AbsoluteJump(jump,(uintptr_t)Load);
    if(!NativeBridge::PatchCode((void *)body,jump,sizeof(jump)))return false;
    installed=true;
    Log(@"controller_sprite installed=1 native_atlas=1 xbox_ps4_ps5=1 profile=%s",RuntimeProfileName());
    return true;
}
static void *GameLoader(void *game) {
    if(!installed||!game||*(void **)game!=gameClass)return nullptr;
    void *loader=*(void **)((char *)game+loaderOffset);
    return loader&&*(void **)loader==loaderClass?loader:nullptr;
}
static void *SpriteFor(void *sprite,void *loader,void *category,Skin skin) {
    if(!sprite)return nullptr;
    char name[64];void *n=Property(sprite,"get_name");if(!ASCII(n,name,sizeof(name)))return nullptr;
    const char *replacement=DesktopControllerSprites::Replacement(name,skin);if(!replacement)return nullptr;
    void *key=Text(replacement),*args[]={key,category};
    // The installed loader is safe here: replacement is already the chosen
    // skin, so the wrapper does not remap it a second time.
    return key?Call(loaderInfo,loader,args):nullptr;
}
static bool RefreshImages(void *game) {
    if(!NSThread.isMainThread)return false;
    void *loader=GameLoader(game);if(!loader)return false;
    static void *imageType,*finder,*getSprite,*setSprite,*setOverride;
    static size_t overrideOffset;
    if(!imageType){
        void *cls=findClass("UnityEngine.UI.dll","UnityEngine.UI","Image");if(!cls)return false;
        imageType=TeleportCursorBridge::TypeObject("UnityEngine.UI.dll","UnityEngine.UI","Image");
        finder=TeleportCursorBridge::OneTypeArgument(findClass("UnityEngine.CoreModule.dll","UnityEngine","Resources"),"FindObjectsOfTypeAll");
        getSprite=ControlDevice::Method(cls,"get_sprite","Sprite",{});
        setSprite=ControlDevice::Method(cls,"set_sprite","Void",{"Sprite"});
        setOverride=ControlDevice::Method(cls,"set_overrideSprite","Void",{"Sprite"});
        if(!NativeChatFocus::FieldOffset(cls,"m_OverrideSprite","Sprite",overrideOffset))overrideOffset=0;
    }
    if(!imageType||!finder||!getSprite||!setSprite)return false;
    void *args[]={imageType},*array=Call(finder,nullptr,args);if(!array)return false;
    uint64_t count=*(uint64_t *)((char *)array+24);if(count>32768)return false;
    void *category=Text(DesktopControllerSprites::category);if(!category)return false;
    Skin skin=DesktopControllerIdentity::skin.load();unsigned replaced=0;
    // One bounded pass per identity revision, including cached inactive UI.
    // Never performed by the mouse/frame loop or during a steady drag.
    for(uint64_t i=0;i<count;++i){
        void *image=((void **)((char *)array+32))[i];if(!image)continue;
        if(void *sprite=SpriteFor(Call(getSprite,image),loader,category,skin)){
            void *a[]={sprite};if(ControlDevice::VoidCall(setSprite,image,a))++replaced;
        }
        // Preserve null override semantics; never create an override merely
        // because the getter falls back to the regular sprite.
        if(overrideOffset&&setOverride){
            void *current=*(void **)((char *)image+overrideOffset);
            if(void *sprite=SpriteFor(current,loader,category,skin)){
                void *a[]={sprite};if(ControlDevice::VoidCall(setOverride,image,a))++replaced;
            }
        }
    }
    Log(@"controller_sprite refreshed=%u images=%llu skin=%u identity_change_only=1",replaced,count,(unsigned)skin);
    return true;
}

static void QueueRefresh() {
    if(refreshQueued.exchange(true))return;
    dispatch_async(dispatch_get_main_queue(),^{
        refreshQueued=false;
        if(!installed||!DesktopControlPreference::gameplayReady.load()||!NativeBridge::IsForeground())return;
        uint64_t revision=DesktopControllerIdentity::revision.load();
        if(appliedRevision==revision)return;
        char *owner=VerifiedInputOwner();
        void *game=owner?*(void **)(owner+GenshinOffsets().ownerGameInput):nullptr;
        if(RefreshImages(game))appliedRevision=revision;
    });
}

static void Install() {
    if(!IsModernRuntime())return;
    DesktopControllerIdentity::changed=QueueRefresh;
    DesktopControllerIdentity::InstallTransportMonitor();
    [NSTimer scheduledTimerWithTimeInterval:1 repeats:YES block:^(NSTimer *){QueueRefresh();}];
    if(TryInstall())return;
    __block unsigned attempts=0;
    [NSTimer scheduledTimerWithTimeInterval:.5 repeats:YES block:^(NSTimer *timer){
        // Waiting at the title or account screen must not exhaust the install attempts.
        if(!DesktopControlPreference::gameplayReady.load()||!NativeBridge::IsForeground())return;
        if(TryInstall()||++attempts>=120)[timer invalidate];
    }];
}
}
