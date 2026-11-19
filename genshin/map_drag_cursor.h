// Included in NativeCameraPointer. The map UI and this noninteractive image
// are drawn by Unity into the same drawable, from the same mouse sample.
namespace MapDragCursor {
static void *createInstance,*objectForID,*destroy,*objectType,*imageType,*canvasType;
static void *addComponent,*getCanvas,*screenToLocal,*setPosition,*setSize;
static void *gameObject,*graphic,*rect,*parent,*canvasCamera;
static void *displayTexture;
static int textureID;
static bool resolved,shown;
static Vector2 lastPoint{},lastPixelSize{};

static bool Resolve() {
    if(resolved)return true;
    void *object=findClass("UnityEngine.CoreModule.dll","UnityEngine","Object");
    void *go=findClass("UnityEngine.CoreModule.dll","UnityEngine","GameObject");
    void *component=findClass("UnityEngine.CoreModule.dll","UnityEngine","Component");
    void *rt=findClass("UnityEngine.CoreModule.dll","UnityEngine","RectTransform");
    void *utility=findClass("UnityEngine.UIModule.dll","UnityEngine","RectTransformUtility");
    void *activator=findClass("mscorlib.dll","System","Activator");
    createInstance=TeleportCursorBridge::OneTypeArgument(activator,"CreateInstance");
    objectType=TeleportCursorBridge::TypeObject("UnityEngine.CoreModule.dll","UnityEngine","GameObject");
    imageType=TeleportCursorBridge::TypeObject("UnityEngine.UI.dll","UnityEngine.UI","RawImage");
    canvasType=TeleportCursorBridge::TypeObject("UnityEngine.UIModule.dll","UnityEngine","Canvas");
    objectForID=Method(object,"FindObjectFromInstanceID","Object",{"Int32"});
    destroy=Method(object,"Destroy","Void",{"Object"});
    addComponent=TeleportCursorBridge::OneTypeArgument(go,"AddComponent");
    getCanvas=TeleportCursorBridge::OneTypeArgument(component,"GetComponentInParent");
    screenToLocal=utility?lookup(utility,"ScreenPointToLocalPointInRectangle",4,0):nullptr;
    setPosition=Method(rt,"set_anchoredPosition","Void",{"Vector2"});
    setSize=Method(rt,"set_sizeDelta","Void",{"Vector2"});
    resolved=createInstance&&objectType&&imageType&&canvasType&&objectForID&&destroy&&
        addComponent&&getCanvas&&screenToLocal&&setPosition&&setSize;
    return resolved;
}
static void Set(void *object,const char *setter,void *argument) {
    void *args[]={argument};
    Call(lookup(*(void **)object,setter,1,0),object,args);
}
static void Hide() {
    if(!shown)return;
    bool enabled=false;Set(graphic,"set_enabled",&enabled);
    shown=false;mapRendersCursor=false;ScheduleGameCursorShape();
}
// The screen-space UI camera samples colour textures as sRGB, while its UI
// target already holds display-encoded values. Upload the exact desktop PNG
// into a linear-sampled texture so that its RGB and straight alpha survive
// that pass. Only this map image uses the copy.
static void DestroyTexture() {
    if(displayTexture) {
        void *args[]={displayTexture};
        Call(destroy,nullptr,args);
        displayTexture=nullptr;
    }
}
static void *DisplayTexture(void *source) {
    void *textureClass=*(void **)source;
    void *formatter=findClass("mscorlib.dll","System.Runtime.Serialization","FormatterServices");
    void *imageConversion=findClass("UnityEngine.ImageConversionModule.dll","UnityEngine","ImageConversion");
    void *uninitialized=TeleportCursorBridge::OneTypeArgument(formatter,"GetUninitializedObject");
    void *textureType=TeleportCursorBridge::TypeObject("UnityEngine.CoreModule.dll","UnityEngine","Texture2D");
    void *ctor=Method(textureClass,".ctor","Void",{"Int32","Int32","TextureFormat","Boolean","Boolean"});
    void *encode=imageConversion?lookup(imageConversion,"EncodeToPNG",1,0):nullptr;
    void *load=imageConversion?lookup(imageConversion,"LoadImage",3,0):nullptr;
    if(!ctor || !uninitialized || !textureType || !encode || !load)return nullptr;
    void *sourceArgs[]={source},*png=Call(encode,nullptr,sourceArgs);
    if(!png)return nullptr;
    void *typeArgs[]={textureType},*copy=Call(uninitialized,nullptr,typeArgs);
    if(!copy)return nullptr;
    int side=2,rgba32=4;
    bool mipmaps=false,linear=true,discardCPU=true;
    void *ctorArgs[]={&side,&side,&rgba32,&mipmaps,&linear},*exception=nullptr;
    invoke(ctor,copy,ctorArgs,&exception);
    if(exception)return nullptr;
    void *loadArgs[]={copy,png,&discardCPU},*ok=Call(load,nullptr,loadArgs);
    if(!ok || !*(bool *)((char *)ok+16)) {
        void *args[]={copy};Call(destroy,nullptr,args);
        return nullptr;
    }
    return copy;
}
static void Release() {
    Hide();
    if(gameObject) {void *args[]={gameObject};Call(destroy,nullptr,args);}
    DestroyTexture();
    gameObject=graphic=rect=parent=canvasCamera=nullptr;textureID=0;
}
static bool Local(Vector2 pixel,Vector2 &point) {
    return ((bool (*)(void *,Vector2,void *,Vector2 *,void *))ManagedMethodCode(screenToLocal))(
        parent,pixel,canvasCamera,&point,screenToLocal);
}
static bool Create(void *page) {
    void *args[]={canvasType};
    void *canvas=Call(getCanvas,page,args);
    if(!canvas)return false;
    void *root=Property(canvas,"get_rootCanvas");
    if(root)canvas=root;
    void *mode=Property(canvas,"get_renderMode");
    parent=Property(canvas,"get_transform");
    if(!mode||!parent||strcmp(className(*(void **)parent),"RectTransform"))return false;
    int renderMode=*(int *)((char *)mode+16);
    canvasCamera=renderMode?Property(canvas,"get_worldCamera"):nullptr;
    if(renderMode && !canvasCamera)return false;
    void *goArgs[]={objectType};
    gameObject=Call(createInstance,nullptr,goArgs);
    if(!gameObject)return false;
    void *label=EngineFunction<void *(*)(const char *)>(0x10e9bb794)("Desktop map cursor");
    Set(gameObject,"set_name",label);
    void *layer=Property(Property(canvas,"get_gameObject"),"get_layer");
    if(layer)Set(gameObject,"set_layer",(char *)layer+16);
    void *imageArgs[]={imageType};
    graphic=Call(addComponent,gameObject,imageArgs);
    rect=graphic?Property(graphic,"get_rectTransform"):nullptr;
    if(!rect){Release();return false;}
    bool off=false;
    Set(graphic,"set_enabled",&off);
    Set(graphic,"set_raycastTarget",&off);
    void *parentArgs[]={parent,&off};
    Call(lookup(*(void **)rect,"SetParent",2,0),rect,parentArgs);
    Call(lookup(*(void **)rect,"SetAsLastSibling",0,0),rect);
    // Anchored coordinates now have the parent's local origin at its pivot.
    void *pivot=Property(parent,"get_pivot");
    if(!pivot){Release();return false;}
    Set(rect,"set_anchorMin",(char *)pivot+16);
    Set(rect,"set_anchorMax",(char *)pivot+16);
    Vector2 topLeft{0,1};Set(rect,"set_pivot",&topLeft);
    return true;
}
static bool Position(Vector2 pixel,int w,int h,bool first) {
    Vector2 size{w*cursorWidthUV.load(),h*cursorHeightUV.load()};
    bool resize=first || size.x!=lastPixelSize.x || size.y!=lastPixelSize.y;
    Vector2 top{pixel.x-w*cursorHotXUV.load(),pixel.y+h*cursorHotYUV.load()};
    Vector2 point;
    if(!Local(top,point))return false;
    if(resize) {
        Vector2 bottom{top.x+size.x,top.y-size.y},other;
        if(!Local(bottom,other))return false;
        Vector2 localSize{other.x-point.x,point.y-other.y};
        if(!(localSize.x>0 && localSize.y>0))return false;
        ((void (*)(void *,Vector2,void *))ManagedMethodCode(setSize))(rect,localSize,setSize);
        lastPixelSize=size;
    }
    if(resize || lastPoint.x!=point.x || lastPoint.y!=point.y) {
        ((void (*)(void *,Vector2,void *))ManagedMethodCode(setPosition))(rect,point,setPosition);
        lastPoint=point;
    }
    return true;
}
// Button state does not select a renderer, texture, material, or tint.
static void Draw(void *page,Vector2 pixel,int w,int h) {
    int currentID=gameCursorTextureID.load();
    if(!currentID || cursorWidthUV.load()<=0) {Hide();return;}
    if(shown && textureID==currentID) {Position(pixel,w,h,false);return;}
    if(!Resolve()) {static bool logged;if(!logged){logged=true;Log(@"map_cursor canvas_unavailable create=%d texture_lookup=%d raw_image=%d rect_position=%d",createInstance!=nullptr,objectForID!=nullptr,imageType!=nullptr,screenToLocal!=nullptr);}return;}
    if(!gameObject && !Create(page))return;
    if(textureID!=currentID) {
        void *args[]={&currentID},*texture=Call(objectForID,nullptr,args);
        if(!texture)return;
        void *copy=DisplayTexture(texture);
        Set(graphic,"set_texture",copy?copy:texture);
        DestroyTexture();displayTexture=copy;
        Log(@"map_cursor color_source=%s source_id=%d",copy?"desktop_png_linear":"original",currentID);
        textureID=currentID;
    }
    if(!Position(pixel,w,h,true))return;
    bool enabled=true;Set(graphic,"set_enabled",&enabled);
    shown=true;mapRendersCursor=true;ScheduleGameCursorShape();
    static unsigned reports;
    if(++reports<=3)Log(@"map_cursor renderer=game_canvas scope=map_hover_and_drag texture_id=%d raycast=0 size_px=(%.1f,%.1f)",
        currentID,lastPixelSize.x,lastPixelSize.y);
}
}
