#include "SwitchEngine.h"

#include "../core/LayoutSession.h"
#include "../core/Utf8Utils.h"

#include <QString>

#include <ApplicationServices/ApplicationServices.h>
#include <Carbon/Carbon.h>
#include <Cocoa/Cocoa.h>

#include <unistd.h>
#include <vector>

namespace {

constexpr int64_t kMark = 0x52736B31;

struct HeldKey {
    CGKeyCode    code  = 0;
    CGEventFlags flags = 0;
};

bool hasLang(TISInputSourceRef src, CFStringRef prefix) {
    if (!src) return false;
    CFArrayRef langs = (CFArrayRef)TISGetInputSourceProperty(src, kTISPropertyInputSourceLanguages);
    if (!langs) return false;
    const CFIndex n = CFArrayGetCount(langs);
    for (CFIndex i = 0; i < n; ++i) {
        CFStringRef s = (CFStringRef)CFArrayGetValueAtIndex(langs, i);
        if (s && CFStringHasPrefix(s, prefix)) return true;
    }
    return false;
}

struct Layouts {
    TISInputSourceRef en = nullptr;
    TISInputSourceRef ru = nullptr;

    void clear() {
        if (en) CFRelease(en);
        if (ru) CFRelease(ru);
        en = nullptr;
        ru = nullptr;
    }

    void refresh() {
        clear();
        const void* keys[] = {kTISPropertyInputSourceCategory};
        const void* vals[] = {kTISCategoryKeyboardInputSource};
        CFDictionaryRef filter = CFDictionaryCreate(
            kCFAllocatorDefault, keys, vals, 1,
            &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
        CFArrayRef list = TISCreateInputSourceList(filter, false);
        CFRelease(filter);
        if (!list) return;
        const CFIndex n = CFArrayGetCount(list);
        for (CFIndex i = 0; i < n; ++i) {
            TISInputSourceRef src = (TISInputSourceRef)CFArrayGetValueAtIndex(list, i);
            CFStringRef type = (CFStringRef)TISGetInputSourceProperty(src, kTISPropertyInputSourceType);
            if (!type || !CFEqual(type, kTISTypeKeyboardLayout)) continue;
            if (!en && hasLang(src, CFSTR("en"))) en = (TISInputSourceRef)CFRetain(src);
            if (!ru && hasLang(src, CFSTR("ru"))) ru = (TISInputSourceRef)CFRetain(src);
        }
        CFRelease(list);
    }

    ~Layouts() { clear(); }
};

struct State {
    CFMachPortRef      tap    = nullptr;
    CFRunLoopSourceRef source = nullptr;
    bool               hold   = false;
    int                injecting = 0;
    bool               prompted  = false;
    std::vector<HeldKey> queued;
    Layouts            layouts;
};

State& state() {
    static State s;
    return s;
}

punto::CharMapping::Layout readLayout() {
    TISInputSourceRef cur = TISCopyCurrentKeyboardInputSource();
    if (!cur) return punto::CharMapping::Layout::Unknown;
    punto::CharMapping::Layout lay = punto::CharMapping::Layout::Unknown;
    if (hasLang(cur, CFSTR("ru"))) lay = punto::CharMapping::Layout::Russian;
    else if (hasLang(cur, CFSTR("en"))) lay = punto::CharMapping::Layout::English;
    CFRelease(cur);
    return lay;
}

std::string fromUni(const UniChar* chars, UniCharCount len) {
    if (!chars || len == 0) return {};
    const QString s = QString::fromUtf16(reinterpret_cast<const char16_t*>(chars), static_cast<int>(len));
    const QByteArray b = s.toUtf8();
    return std::string(b.constData(), static_cast<size_t>(b.size()));
}

std::string translate(CGKeyCode code, CGEventFlags flags, TISInputSourceRef source) {
    if (!source) return {};
    CFDataRef data = (CFDataRef)TISGetInputSourceProperty(source, kTISPropertyUnicodeKeyLayoutData);
    if (!data) return {};
    const UCKeyboardLayout* layout = reinterpret_cast<const UCKeyboardLayout*>(CFDataGetBytePtr(data));
    UInt32 mods = 0;
    if (flags & kCGEventFlagMaskShift) mods |= (shiftKey >> 8);
    if (flags & kCGEventFlagMaskAlphaShift) mods |= (alphaLock >> 8);
    UInt32 dead = 0;
    UniChar chars[8];
    UniCharCount len = 0;
    const OSStatus st = UCKeyTranslate(layout, code, kUCKeyActionDown, mods, LMGetKbdType(),
                                        kUCKeyTranslateNoDeadKeysBit, &dead, 8, &len, chars);
    if (st != noErr || len == 0) return {};
    return fromUni(chars, len);
}

std::string translateCurrent(CGKeyCode code, CGEventFlags flags) {
    TISInputSourceRef src = TISCopyCurrentKeyboardLayoutInputSource();
    const std::string s = translate(code, flags, src);
    if (src) CFRelease(src);
    return s;
}

void postStamped(State* st, CGEventRef event) {
    CGEventSetIntegerValueField(event, kCGEventSourceUserData, kMark);
    ++st->injecting;
    CGEventPost(kCGHIDEventTap, event);
    --st->injecting;
}

void postBackspace(State* st) {
    CGEventSourceRef src = CGEventSourceCreate(kCGEventSourceStatePrivate);
    CGEventRef down = CGEventCreateKeyboardEvent(src, 0x33, true);
    CGEventRef up = CGEventCreateKeyboardEvent(src, 0x33, false);
    postStamped(st, down);
    postStamped(st, up);
    CFRelease(down);
    CFRelease(up);
    if (src) CFRelease(src);
}

void postText(State* st, const std::string& utf8) {
    if (utf8.empty()) return;
    const QString s = QString::fromUtf8(utf8.data(), static_cast<int>(utf8.size()));
    if (s.isEmpty()) return;
    std::vector<UniChar> chars(static_cast<size_t>(s.size()));
    const ushort* u = s.utf16();
    for (int i = 0; i < s.size(); ++i) chars[static_cast<size_t>(i)] = u[i];
    CGEventSourceRef src = CGEventSourceCreate(kCGEventSourceStatePrivate);
    CGEventRef down = CGEventCreateKeyboardEvent(src, 0, true);
    CGEventRef up = CGEventCreateKeyboardEvent(src, 0, false);
    CGEventKeyboardSetUnicodeString(down, static_cast<UniCharCount>(chars.size()), chars.data());
    CGEventKeyboardSetUnicodeString(up, static_cast<UniCharCount>(chars.size()), chars.data());
    postStamped(st, down);
    postStamped(st, up);
    CFRelease(down);
    CFRelease(up);
    if (src) CFRelease(src);
}

void postUserKey(CGKeyCode code, CGEventFlags flags) {
    CGEventRef down = CGEventCreateKeyboardEvent(nullptr, code, true);
    CGEventRef up = CGEventCreateKeyboardEvent(nullptr, code, false);
    CGEventSetFlags(down, flags);
    CGEventSetFlags(up, flags);
    CGEventPost(kCGHIDEventTap, down);
    CGEventPost(kCGHIDEventTap, up);
    CFRelease(down);
    CFRelease(up);
}

void selectLayout(State* st, bool russian) {
    st->layouts.refresh();
    TISInputSourceRef src = russian ? st->layouts.ru : st->layouts.en;
    if (!src) {
        SwitchEngine::log(russian ? QStringLiteral("no russian layout")
                                  : QStringLiteral("no english layout"));
        return;
    }
    TISSelectInputSource(src);
}

void playSound() {
    static NSSound* playing = nil;
    NSString* path = [[NSBundle mainBundle] pathForResource:@"switch" ofType:@"wav" inDirectory:@"sounds"];
    if (!path) return;
    playing = [[NSSound alloc] initWithContentsOfFile:path byReference:NO];
    [playing play];
}

bool replaceLater(State* st, const SwitchEngine::KeyOutcome& outcome) {
    if (outcome.kind != SwitchEngine::KeyOutcome::Kind::Replace) return false;
    st->hold = true;
    const std::string insert = outcome.insert;
    const int deletes = outcome.deleteCount;
    const bool toRu = outcome.switchToRussian;
    const bool toEn = outcome.switchToEnglish;
    const bool sound = outcome.playSound;
    dispatch_async(dispatch_get_main_queue(), ^{
        for (int i = 0; i < deletes; ++i) {
            postBackspace(st);
            usleep(1500);
        }
        postText(st, insert);
        if (toRu) selectLayout(st, true);
        if (toEn) selectLayout(st, false);
        if (sound) playSound();
        st->hold = false;
        const std::vector<HeldKey> queued = std::move(st->queued);
        st->queued.clear();
        for (const HeldKey& key : queued) postUserKey(key.code, key.flags);
    });
    return true;
}

CGEventRef handleEvent(CGEventType type, CGEventRef event, State* st) {
    if (type == kCGEventTapDisabledByTimeout || type == kCGEventTapDisabledByUserInput) {
        if (st->tap) CGEventTapEnable(st->tap, true);
        return event;
    }
    if (!event) return event;
    if (st->injecting > 0 || CGEventGetIntegerValueField(event, kCGEventSourceUserData) == kMark)
        return event;

    if (type == kCGEventLeftMouseDown || type == kCGEventRightMouseDown
        || type == kCGEventOtherMouseDown) {
        if (!st->hold) SwitchEngine::instance().onReset();
        return event;
    }
    if (type == kCGEventFlagsChanged) return event;
    if (type == kCGEventKeyUp) return st->hold ? nullptr : event;
    if (type != kCGEventKeyDown) return event;

    if (IsSecureEventInputEnabled()) {
        SwitchEngine::instance().onReset();
        return event;
    }

    const auto code = static_cast<CGKeyCode>(CGEventGetIntegerValueField(event, kCGKeyboardEventKeycode));
    const CGEventFlags flags = CGEventGetFlags(event);
    if (st->hold) {
        st->queued.push_back(HeldKey{code, flags});
        return nullptr;
    }

    if (flags & (kCGEventFlagMaskCommand | kCGEventFlagMaskControl | kCGEventFlagMaskAlternate)) {
        SwitchEngine::instance().onReset();
        return event;
    }

    auto& engine = SwitchEngine::instance();
    if (st->layouts.en == nullptr || st->layouts.ru == nullptr) st->layouts.refresh();
    const auto layout = readLayout();

    if (code == 0x33) {
        engine.onBackspace();
        return event;
    }
    // Forward delete, arrows, home/end, page, escape: the caret left the word.
    if (code == 0x75 || code == 0x7B || code == 0x7C || code == 0x7D || code == 0x7E
        || code == 0x73 || code == 0x77 || code == 0x74 || code == 0x79 || code == 0x35) {
        engine.onReset();
        return event;
    }
    if (code == 0x31 || code == 0x24 || code == 0x30) {
        const std::string boundary = code == 0x31 ? " " : code == 0x30 ? "\t" : "\n";
        if (replaceLater(st, engine.onBoundary(layout, boundary, boundary))) return nullptr;
        return event;
    }

    const std::string cur = translateCurrent(code, flags);
    TISInputSourceRef otherSrc = layout == punto::CharMapping::Layout::English ? st->layouts.ru
                               : layout == punto::CharMapping::Layout::Russian ? st->layouts.en
                                                                               : nullptr;
    const std::string oth = translate(code, flags, otherSrc);
    if (punto::LayoutSession::isWordText(cur) || punto::LayoutSession::isWordText(oth)) {
        engine.onWordChar(layout, cur, oth);
        return event;
    }
    if (cur.empty() && oth.empty()) return event;
    const std::string boundaryOther = oth.empty() ? cur : oth;
    if (replaceLater(st, engine.onBoundary(layout, cur.empty() ? boundaryOther : cur, boundaryOther)))
        return nullptr;
    return event;
}

CGEventRef tapCallback(CGEventTapProxy, CGEventType type, CGEventRef event, void* user) {
    return handleEvent(type, event, static_cast<State*>(user));
}

bool installTap() {
    State& st = state();
    if (st.tap) return true;
    if (!st.prompted) {
        st.prompted = true;
        NSDictionary* opts = @{(__bridge id)kAXTrustedCheckOptionPrompt: @YES};
        AXIsProcessTrustedWithOptions((__bridge CFDictionaryRef)opts);
    }
    if (!AXIsProcessTrusted()) return false;
    const CGEventMask mask = CGEventMaskBit(kCGEventKeyDown)
                           | CGEventMaskBit(kCGEventKeyUp)
                           | CGEventMaskBit(kCGEventFlagsChanged)
                           | CGEventMaskBit(kCGEventLeftMouseDown)
                           | CGEventMaskBit(kCGEventRightMouseDown)
                           | CGEventMaskBit(kCGEventOtherMouseDown);
    st.tap = CGEventTapCreate(kCGSessionEventTap, kCGHeadInsertEventTap, kCGEventTapOptionDefault,
                              mask, &tapCallback, &st);
    if (!st.tap) {
        SwitchEngine::log(QStringLiteral("tap create failed"));
        return false;
    }
    st.source = CFMachPortCreateRunLoopSource(kCFAllocatorDefault, st.tap, 0);
    CFRunLoopAddSource(CFRunLoopGetMain(), st.source, kCFRunLoopCommonModes);
    CGEventTapEnable(st.tap, true);
    st.layouts.refresh();
    SwitchEngine::log(QStringLiteral("tap started en=%1 ru=%2")
                          .arg(st.layouts.en ? 1 : 0)
                          .arg(st.layouts.ru ? 1 : 0));
    return true;
}

} // namespace

void SwitchEngine::start() {
    command(QStringLiteral("RELOAD"));
    running_ = installTap();
    if (!running_) SwitchEngine::log(QStringLiteral("accessibility off"));
    activateApp();
    static dispatch_source_t timer = nil;
    if (!timer) {
        timer = dispatch_source_create(DISPATCH_SOURCE_TYPE_TIMER, 0, 0, dispatch_get_main_queue());
        dispatch_source_set_timer(timer, dispatch_time(DISPATCH_TIME_NOW, 0),
                                  500 * NSEC_PER_MSEC, 50 * NSEC_PER_MSEC);
        dispatch_source_set_event_handler(timer, ^{
            const bool up = installTap();
            if (up && !running_) SwitchEngine::log(QStringLiteral("tap started late"));
            running_ = up;
            setLayout(readLayout());
        });
        dispatch_resume(timer);
    }
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 300 * NSEC_PER_MSEC),
                   dispatch_get_main_queue(), ^{
        [NSApp setActivationPolicy:NSApplicationActivationPolicyAccessory];
    });
}

void SwitchEngine::activateApp() {
    dispatch_async(dispatch_get_main_queue(), ^{
        [NSApp setActivationPolicy:NSApplicationActivationPolicyAccessory];
        if (@available(macOS 14.0, *))
            [NSApp activate];
        else
            [NSApp activateIgnoringOtherApps:YES];
    });
}
