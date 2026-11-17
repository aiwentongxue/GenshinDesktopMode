#pragma once
#include <cstring>
#include "controller_ui_policy.h"
namespace DesktopControllerSprites {
struct Pair {const char *colored,*white;};
static constexpr const char *category="ART/UI/Atlas/InputKeyIcon/";
static constexpr Pair pairs[]={
    {"UI_KeyPs4_0","UI_KeyPs5_0"}, // square
    {"UI_KeyPs4_1","UI_KeyPs5_1"}, // cross
    {"UI_KeyPs4_2","UI_KeyPs5_2"}, // circle
    {"UI_KeyPs4_3","UI_KeyPs5_3"}, // triangle
    {"UI_KeyPs4_13","UI_KeyPs5_13"}, // touchpad
    {"UI_KeyCom_Up","UI_KeyCom_Ps5_Up"},
    {"UI_KeyCom_Right","UI_KeyCom_Ps5_Right"},
    {"UI_KeyCom_Down","UI_KeyCom_Ps5_Down"},
    {"UI_KeyCom_Left","UI_KeyCom_Ps5_Left"},
    {"UI_KeyCom_Horizontal","UI_KeyCom_Ps5_Horizontal"},
    {"UI_KeyCom_Vertical","UI_KeyCom_Ps5_Vertical"}
};
inline const char *Replacement(const char *key,DesktopControllerUI::Skin skin) {
    if(!key)return nullptr;
    bool white=skin==DesktopControllerUI::Skin::PSWhite;
    for(const auto &p:pairs)if(!strcmp(key,white?p.colored:p.white))return white?p.white:p.colored;
    return nullptr;
}
}
