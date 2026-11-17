#pragma once
#include <cstdint>

// One bounded focus request. Page IDs are observations, never retained Unity
// object pointers. A page may be replaced only by the requested mode handoff.
struct DesktopChatFocusRequest {
    enum class Step {Wait,Activate,Complete,Cancel};
    int page=0,fromMode=0,targetMode=0,stablePage=0;
    unsigned activations=0;
    double expires=0,nextActivation=0;
    bool replacementUsed=false;
    void begin(int id,int from,int target,double now) {
        *this={};page=id;fromMode=from;targetMode=target;expires=now+1.1;
    }
    Step observe(int id,int mode,bool rebuilding,bool editing,double now) {
        if(!page||now>expires)return Step::Cancel;
        if(mode!=targetMode||rebuilding){stablePage=0;return Step::Wait;}
        if(!id)return fromMode!=targetMode&&!replacementUsed?Step::Wait:Step::Cancel;
        if(id!=page){
            if(fromMode==targetMode||replacementUsed)return Step::Cancel;
            page=id;replacementUsed=true;stablePage=0;
        }
        if(editing)return Step::Complete;
        // Allow the native button handler and page OnEnable to finish before
        // requesting focus, then verify the responder on the following pass.
        if(stablePage!=id){stablePage=id;return Step::Wait;}
        if(now<nextActivation)return Step::Wait;
        if(activations>=3)return Step::Cancel;
        ++activations;nextActivation=now+.15;return Step::Activate;
    }
};
