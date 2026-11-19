#pragma once
#include <cstdint>

namespace DesktopControllerUI {
enum class Model : uint8_t {Unknown, Xbox, DualShock, DualSense, PlayStation};
enum class Transport : uint8_t {Unknown, USB, Bluetooth};
enum class Skin : uint8_t {Native, Xbox, PSColored, PSWhite};
struct EndpointSummary {
    unsigned count=0;
    Transport transport=Transport::Unknown;
    void add(Transport t) {++count;transport=count==1?t:Transport::Unknown;}
};
inline Transport ResolveTransport(unsigned matchingControllers,const EndpointSummary &endpoints) {
    // GameController exposes no public per-device HID identity. Only associate
    // an endpoint when both sides are unique within this physical model.
    return matchingControllers==1&&endpoints.count==1?endpoints.transport:Transport::Unknown;
}
inline Skin Resolve(Model model,Transport transport) {
    if(model==Model::Xbox)return Skin::Xbox;
    if(model==Model::DualSense&&transport==Transport::USB)return Skin::PSWhite;
    if(model==Model::DualSense||model==Model::DualShock||model==Model::PlayStation)return Skin::PSColored;
    return Skin::Native;
}
}
