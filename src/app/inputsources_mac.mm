#include "inputsources.h"
#import <Carbon/Carbon.h>
#import <Foundation/Foundation.h>

namespace InputSources {
namespace {
QString qt(CFTypeRef value) {
    return value && CFGetTypeID(value)==CFStringGetTypeID() ? QString::fromNSString((__bridge NSString*)value) : QString{};
}
QString primaryLanguage(TISInputSourceRef source) {
    auto languages=(CFArrayRef)TISGetInputSourceProperty(source,kTISPropertyInputSourceLanguages);
    if(!languages || CFArrayGetCount(languages)==0) return {};
    // "zh-Hans", "fr", "en": the first entry is the language the source is made for.
    return qt(CFArrayGetValueAtIndex(languages,0)).section(u'-',0,0).toLower();
}
bool flag(TISInputSourceRef source,CFStringRef key) {
    auto value=(CFBooleanRef)TISGetInputSourceProperty(source,key);
    return value && CFBooleanGetValue(value);
}
}

QList<Source> enabled() {
    @autoreleasepool {
        QList<Source> result;
        NSDictionary* filter=@{(__bridge NSString*)kTISPropertyInputSourceCategory:(__bridge NSString*)kTISCategoryKeyboardInputSource};
        CFArrayRef list=TISCreateInputSourceList((__bridge CFDictionaryRef)filter,false);
        if(!list) return result;
        for(CFIndex i=0;i<CFArrayGetCount(list);++i) {
            auto source=(TISInputSourceRef)CFArrayGetValueAtIndex(list,i);
            if(!flag(source,kTISPropertyInputSourceIsSelectCapable) || !flag(source,kTISPropertyInputSourceIsEnabled)) continue;
            // An input method with modes is selected through one of its modes (Pinyin, Zhuyin…).
            if(qt(TISGetInputSourceProperty(source,kTISPropertyInputSourceType))==qt(kTISTypeKeyboardInputMethodModeEnabled)) continue;
            result.append({qt(TISGetInputSourceProperty(source,kTISPropertyInputSourceID)),
                qt(TISGetInputSourceProperty(source,kTISPropertyLocalizedName)),primaryLanguage(source)});
        }
        CFRelease(list);
        return result;
    }
}

QString current() {
    TISInputSourceRef source=TISCopyCurrentKeyboardInputSource();
    if(!source) return {};
    const auto id=qt(TISGetInputSourceProperty(source,kTISPropertyInputSourceID));
    CFRelease(source);
    return id;
}

bool select(const QString& id) {
    @autoreleasepool {
        NSDictionary* filter=@{(__bridge NSString*)kTISPropertyInputSourceID:id.toNSString()};
        CFArrayRef list=TISCreateInputSourceList((__bridge CFDictionaryRef)filter,false);
        if(!list) return false;
        bool ok=false;
        if(CFArrayGetCount(list)>0) ok=TISSelectInputSource((TISInputSourceRef)CFArrayGetValueAtIndex(list,0))==noErr;
        CFRelease(list);
        return ok;
    }
}

QString settingsUrl() { return QStringLiteral("x-apple.systempreferences:com.apple.Keyboard-Settings.extension"); }

}
