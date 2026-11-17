#include "../genshin/web_escape_policy.h"
#include <cassert>
#include <iostream>
int main() {
    for(const char *p:{"/ys/event/gacha-v4/index.html","/event/gacha-v3/index.html","/gacha/index.html","/gacha_record/index.html","/gachalog/record","/hk4e/event/e20190909gacha-v3/index.html","/ys/event/e20210609gacha/index.html"}) assert(IsDesktopWishHistoryPath(p));
    for(const char *p:{"/login","/account/index.html","/auth","/event/news/index.html","/index.html","/account/gacha"}) assert(!IsDesktopWishHistoryPath(p));
    assert(!IsDesktopWishHistoryPath(nullptr));
    std::cout<<"Wish-record routes accepted; account/authentication/ordinary web routes excluded\n";
}
