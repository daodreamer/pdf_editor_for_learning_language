#include "aiplatform.h"
#import <AppKit/AppKit.h>
#import <ApplicationServices/ApplicationServices.h>
#import <Carbon/Carbon.h>

namespace AiPlatform {
namespace {
QString bundleId(NSURL* app) {
    if(!app) return {};
    NSBundle* bundle=[NSBundle bundleWithURL:app];
    return bundle.bundleIdentifier ? QString::fromNSString(bundle.bundleIdentifier) : QString{};
}
}

App desktopApp(const QString& target) {
    @autoreleasepool {
        NSString* identifier=target==QLatin1String("claude") ? @"com.anthropic.claudefordesktop"
            : target==QLatin1String("chatgpt") ? @"com.openai.chat" : nil;
        if(!identifier) return {};
        NSURL* url=[[NSWorkspace sharedWorkspace] URLForApplicationWithBundleIdentifier:identifier];
        if(!url) return {};
        return {QString::fromNSString(identifier),QUrl::fromLocalFile(QString::fromNSString(url.path))};
    }
}

QString frontmost() {
    @autoreleasepool {
        NSRunningApplication* app=[NSWorkspace sharedWorkspace].frontmostApplication;
        return app.bundleIdentifier ? QString::fromNSString(app.bundleIdentifier) : QString{};
    }
}

QString self() {
    @autoreleasepool {
        NSString* identifier=[NSRunningApplication currentApplication].bundleIdentifier;
        return identifier ? QString::fromNSString(identifier) : QString{};
    }
}

QString browser() {
    @autoreleasepool {
        return bundleId([[NSWorkspace sharedWorkspace] URLForApplicationToOpenURL:[NSURL URLWithString:@"https://claude.ai"]]);
    }
}

bool pasteAllowed(bool prompt) {
    @autoreleasepool {
        if(!prompt) return AXIsProcessTrusted();
        NSDictionary* options=@{(__bridge NSString*)kAXTrustedCheckOptionPrompt:@YES};
        return AXIsProcessTrustedWithOptions((__bridge CFDictionaryRef)options);
    }
}

bool paste() {
    // Key code of the V key: the same physical key on QWERTY, AZERTY and QWERTZ.
    CGEventSourceRef source=CGEventSourceCreate(kCGEventSourceStateCombinedSessionState);
    CGEventRef down=CGEventCreateKeyboardEvent(source,kVK_ANSI_V,true);
    CGEventRef up=CGEventCreateKeyboardEvent(source,kVK_ANSI_V,false);
    const bool ok=down && up;
    if(ok) {
        CGEventSetFlags(down,kCGEventFlagMaskCommand);
        CGEventSetFlags(up,kCGEventFlagMaskCommand);
        CGEventPost(kCGHIDEventTap,down);
        CGEventPost(kCGHIDEventTap,up);
    }
    if(down) CFRelease(down);
    if(up) CFRelease(up);
    if(source) CFRelease(source);
    return ok;
}

QString permissionSettingsUrl() {
    return QStringLiteral("x-apple.systempreferences:com.apple.preference.security?Privacy_Accessibility");
}

}
