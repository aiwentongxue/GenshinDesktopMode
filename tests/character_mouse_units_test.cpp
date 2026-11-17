#include "../genshin/character_mouse_units.h"
#include <cassert>
#include <fstream>
#include <iterator>
#include <vector>
int main(int argc,char **argv) {
    using namespace CharacterMouseUnits;
    // Identical physical motion must produce identical rotation at 1x/2x
    // render resolution and when resizing the game window.
    assert(std::fabs(fromPixels(20,1500,1500,.1f)-2)<1e-6);
    assert(std::fabs(fromPixels(40,1500,3000,.1f)-2)<1e-6);
    assert(std::fabs(fromPixels(60,1000,3000,.1f)-2)<1e-6);
    assert(fromPixels(40,1500,0,.1f)==0);
    assert(fromPixels(NAN,1500,3000,.1f)==0);
    for(int i=1;i<argc;++i) {
        std::ifstream file(argv[i],std::ios::binary);
        std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(file)),{});
        assert(!bytes.empty());float x=0,y=0;
        assert(sensitivity(bytes.data(),bytes.size(),"Mouse X",0,x));
        assert(sensitivity(bytes.data(),bytes.size(),"Mouse Y",1,y));
        assert(std::fabs(x-.1f)<1e-6&&std::fabs(y-.1f)<1e-6);
        assert(!sensitivity(bytes.data(),bytes.size(),"Mouse X",1,x));
        assert(!sensitivity(bytes.data(),16,"Mouse X",0,x));
    }
}
