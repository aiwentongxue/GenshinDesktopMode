#import <Foundation/Foundation.h>
#include "runtime_image_fixture.h"
#include "../genshin/game_identity.h"
#include <iostream>

int main(int argc,char **argv) {
    assert(argc==4);
    @autoreleasepool {
        NSMutableArray *results=[NSMutableArray array];
        for(int client=1;client<=2;++client) {
            NSString *exe=[NSString stringWithUTF8String:argv[client]];
            NSDictionary *actual=[NSDictionary dictionaryWithContentsOfFile:
                [exe.stringByDeletingLastPathComponent stringByAppendingPathComponent:@"Info.plist"]];
            assert(actual);
            Image image(argv[client]);
            auto reader=[&](uintptr_t a,size_t n,bool w){return image.read(a,n,w);};
            auto expected=client==1?GenshinRuntimeLayout::MainlandInput:GenshinRuntimeLayout::GlobalInput;
            for(NSString *version in @[actual[@"CFBundleShortVersionString"],@"7.2.0",@"99.0.0"]) {
                // Round-trip the changed package metadata without touching the
                // real App. These are precisely the identity values used by
                // the plugin constructor; version fields are not inputs to it.
                NSMutableDictionary *fixture=[actual mutableCopy];
                fixture[@"CFBundleShortVersionString"]=version;
                fixture[@"CFBundleVersion"]=@"99999";
                NSData *encoded=[NSPropertyListSerialization dataWithPropertyList:fixture
                    format:NSPropertyListBinaryFormat_v1_0 options:0 error:nil];
                NSDictionary *decoded=[NSPropertyListSerialization propertyListWithData:encoded
                    options:0 format:nil error:nil];
                assert([decoded[@"CFBundleShortVersionString"] isEqualToString:version]);
                assert(GenshinIdentity::IsGame([decoded[@"CFBundleIdentifier"] UTF8String],exe.UTF8String));
                uintptr_t mismatch=0;
                assert(SelectGenshinRuntimeLayout(reader,mismatch)==expected);
                [results addObject:@{@"bundle":decoded[@"CFBundleIdentifier"],@"display_version":version,
                    @"build":decoded[@"CFBundleVersion"],@"code_unchanged":@YES,@"accepted":@YES}];
            }
        }
        NSDictionary *report=@{@"test":@"metadata-only update; executable UUID also changed in memory",
            @"real_app_metadata_modified":@NO,@"actual_7_2_client_tested":@NO,
            @"meaning":@"Version labels alone do not disable the plugin; a new code layout still needs validation",
            @"cases":results};
        NSData *json=[NSJSONSerialization dataWithJSONObject:report options:NSJSONWritingPrettyPrinted error:nil];
        assert([json writeToFile:[NSString stringWithUTF8String:argv[3]] atomically:YES]);
        std::cout<<"CN/global: changed display version to 7.2.0 and 99.0.0, changed build and UUID; same code remains accepted. Actual 7.2 client not tested.\n";
    }
}
