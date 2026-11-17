// Read-only metadata inspection to identify the UI-specific platform selector.
// No platform, resource, account or network values are changed here.
static void InspectUIUtilityTypes(void) {
    if(!UsesLegacyMetadataLayout()) return;
    if(!IsCompatibleRuntime()) return;
    auto manager=EngineFunction<char *(*)()>(0x10f05b6fc)();
    auto images=*(char ***)(manager+0x208);char *img=images[60];
    uint32_t count=*(uint32_t *)(img+0x30)+0x9bd93914u;
    uint32_t start=*(uint32_t *)(img+0x48)^0x79f2e891u;
    if(count>100000) return;
    auto typeDef=EngineFunction<char *(*)(uint32_t)>(0x10ea0e980);
    auto typeClass=EngineFunction<void *(*)(uint32_t)>(0x10ea0c3b4);
    auto next=EngineFunction<void *(*)(void *,void **)>(0x10e9bb5f8);
    auto className=EngineFunction<const char *(*)(void *)>(0x10e9bb600);
    unsigned matched=0;
    for(uint32_t j=0;j<count;++j) {
        char *def=typeDef(start+j);if(!def) continue;
        unsigned methods=*(uint16_t *)(def+0x32)^0xe0a2u;
        if(methods<80 || methods>2000) continue;
        void *cls=typeClass(start+j),*it=nullptr;if(!cls) continue;
        bool match=false;
        while(void *method=next(cls,&it)) {
            uintptr_t code=ManagedMethodCode(method)-_dyld_get_image_vmaddr_slide(0);
            if(code==0x107bc1fd0 || code==0x107b780e8) {match=true;break;}
        }
        if(!match) continue;
        ++matched;
        Log(@"ui_utility_class index=%u name=%s methods=%u",start+j,className(cls),methods);
        InspectInputManagerDetails(start+j);
        InspectMethods(cls,"UIUtility",className(cls));
    }
    Log(@"ui_utility_inspection finished matches=%u",matched);
}
