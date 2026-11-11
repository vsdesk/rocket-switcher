#include "SwitchEngine.h"

#include "../core/LayoutSession.h"

#include <QObject>
#include <QTimer>

#include <windows.h>

#include <cstring>
#include <string>
#include <vector>

namespace {

constexpr ULONG_PTR kMark = 0x52736B31;

struct HeldKey {
    DWORD vk    = 0;
    DWORD scan  = 0;
    DWORD flags = 0;
};

struct State {
    HHOOK hook = nullptr;
    bool  hold = false;
    int   injecting = 0;
    HKL   en = nullptr;
    HKL   ru = nullptr;
    std::vector<HeldKey> queued;
};

State& state() {
    static State s;
    return s;
}

void refreshLayouts(State& st) {
    st.en = nullptr;
    st.ru = nullptr;
    HKL list[32];
    const int n = GetKeyboardLayoutList(32, list);
    for (int i = 0; i < n; ++i) {
        const WORD lang = PRIMARYLANGID(LOWORD(reinterpret_cast<DWORD_PTR>(list[i])));
        if (lang == LANG_ENGLISH && !st.en) st.en = list[i];
        if (lang == LANG_RUSSIAN && !st.ru) st.ru = list[i];
    }
}

HKL foregroundLayout() {
    const HWND fg = GetForegroundWindow();
    const DWORD tid = GetWindowThreadProcessId(fg, nullptr);
    return GetKeyboardLayout(tid);
}

punto::CharMapping::Layout layoutOf(HKL hkl) {
    const WORD lang = PRIMARYLANGID(LOWORD(reinterpret_cast<DWORD_PTR>(hkl)));
    if (lang == LANG_RUSSIAN) return punto::CharMapping::Layout::Russian;
    if (lang == LANG_ENGLISH) return punto::CharMapping::Layout::English;
    return punto::CharMapping::Layout::Unknown;
}

std::string fromWide(const wchar_t* text, int n) {
    if (!text || n <= 0) return {};
    const int bytes = WideCharToMultiByte(CP_UTF8, 0, text, n, nullptr, 0, nullptr, nullptr);
    if (bytes <= 0) return {};
    std::string out(static_cast<size_t>(bytes), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text, n, out.data(), bytes, nullptr, nullptr);
    return out;
}

std::wstring toWide(const std::string& utf8) {
    if (utf8.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()),
                                      nullptr, 0);
    if (n <= 0) return {};
    std::wstring out(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), out.data(), n);
    return out;
}

std::string translateKey(DWORD vk, DWORD scan, const BYTE state[256], HKL hkl) {
    wchar_t buf[8] = {};
    const int n = ToUnicodeEx(vk, scan, state, buf, 8, 0, hkl);
    if (n < 0) {
        ToUnicodeEx(vk, scan, state, buf, 8, 0, hkl);
        return {};
    }
    return fromWide(buf, n);
}

void fillState(BYTE state[256], DWORD vk) {
    std::memset(state, 0, 256);
    if (GetAsyncKeyState(VK_SHIFT) & 0x8000) {
        state[VK_SHIFT] = 0x80;
        state[VK_LSHIFT] = 0x80;
    }
    if (GetKeyState(VK_CAPITAL) & 1) state[VK_CAPITAL] = 0x01;
    state[vk] = 0x80;
}

void sendBackspace(State* st) {
    INPUT in[2] = {};
    in[0].type = INPUT_KEYBOARD;
    in[0].ki.wVk = VK_BACK;
    in[0].ki.dwExtraInfo = kMark;
    in[1] = in[0];
    in[1].ki.dwFlags = KEYEVENTF_KEYUP;
    ++st->injecting;
    SendInput(2, in, sizeof(INPUT));
    --st->injecting;
}

void sendText(State* st, const std::string& utf8) {
    const std::wstring text = toWide(utf8);
    if (text.empty()) return;
    std::vector<INPUT> in;
    in.reserve(text.size() * 2);
    for (wchar_t ch : text) {
        if (ch == 0) continue;
        INPUT down{};
        down.type = INPUT_KEYBOARD;
        down.ki.wScan = ch;
        down.ki.dwFlags = KEYEVENTF_UNICODE;
        down.ki.dwExtraInfo = kMark;
        INPUT up = down;
        up.ki.dwFlags |= KEYEVENTF_KEYUP;
        in.push_back(down);
        in.push_back(up);
    }
    if (in.empty()) return;
    ++st->injecting;
    SendInput(static_cast<UINT>(in.size()), in.data(), sizeof(INPUT));
    --st->injecting;
}

void sendUserKey(const HeldKey& key) {
    INPUT down{};
    down.type = INPUT_KEYBOARD;
    down.ki.wVk = static_cast<WORD>(key.vk);
    down.ki.wScan = static_cast<WORD>(key.scan);
    if (key.flags & LLKHF_EXTENDED) down.ki.dwFlags |= KEYEVENTF_EXTENDEDKEY;
    INPUT up = down;
    up.ki.dwFlags |= KEYEVENTF_KEYUP;
    SendInput(1, &down, sizeof(INPUT));
    SendInput(1, &up, sizeof(INPUT));
}

void selectLayout(State* st, bool russian) {
    refreshLayouts(*st);
    HKL target = russian ? st->ru : st->en;
    const HWND fg = GetForegroundWindow();
    if (!fg || !target) {
        SwitchEngine::log(russian ? QStringLiteral("no russian layout")
                                  : QStringLiteral("no english layout"));
        return;
    }
    PostMessageW(fg, WM_INPUTLANGCHANGEREQUEST, 0, reinterpret_cast<LPARAM>(target));
}

void playSound() {
    wchar_t path[MAX_PATH];
    const DWORD n = GetModuleFileNameW(nullptr, path, MAX_PATH);
    if (!n || n >= MAX_PATH) return;
    wchar_t* slash = wcsrchr(path, L'\\');
    if (!slash) return;
    lstrcpyW(slash + 1, L"sounds\\switch.wav");
    PlaySoundW(path, nullptr, SND_FILENAME | SND_ASYNC | SND_NODEFAULT);
}

bool replaceLater(State* st, const SwitchEngine::KeyOutcome& outcome) {
    if (outcome.kind != SwitchEngine::KeyOutcome::Kind::Replace) return false;
    st->hold = true;
    const std::string insert = outcome.insert;
    const int deletes = outcome.deleteCount;
    const bool toRu = outcome.switchToRussian;
    const bool toEn = outcome.switchToEnglish;
    const bool sound = outcome.playSound;
    QTimer::singleShot(0, [st, insert, deletes, toRu, toEn, sound]() {
        for (int i = 0; i < deletes; ++i) sendBackspace(st);
        sendText(st, insert);
        if (toRu) selectLayout(st, true);
        if (toEn) selectLayout(st, false);
        if (sound) playSound();
        st->hold = false;
        const std::vector<HeldKey> queued = std::move(st->queued);
        st->queued.clear();
        for (const HeldKey& key : queued) sendUserKey(key);
    });
    return true;
}

LRESULT CALLBACK hookProc(int code, WPARAM wParam, LPARAM lParam) {
    State& st = state();
    if (code < 0 || !lParam) return CallNextHookEx(st.hook, code, wParam, lParam);
    const auto* key = reinterpret_cast<KBDLLHOOKSTRUCT*>(lParam);
    if (st.injecting > 0 || key->dwExtraInfo == kMark)
        return CallNextHookEx(st.hook, code, wParam, lParam);

    const bool down = wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN;
    if (st.hold) {
        if (down) st.queued.push_back(HeldKey{key->vkCode, key->scanCode, key->flags});
        return 1;
    }
    if (!down) return CallNextHookEx(st.hook, code, wParam, lParam);

    if ((GetAsyncKeyState(VK_CONTROL) & 0x8000) || (GetAsyncKeyState(VK_MENU) & 0x8000)
        || (GetAsyncKeyState(VK_LWIN) & 0x8000) || (GetAsyncKeyState(VK_RWIN) & 0x8000)) {
        SwitchEngine::instance().onReset();
        return CallNextHookEx(st.hook, code, wParam, lParam);
    }

    auto& engine = SwitchEngine::instance();
    if (!st.en || !st.ru) refreshLayouts(st);
    const HKL current = foregroundLayout();
    const auto layout = layoutOf(current);
    const DWORD vk = key->vkCode;
    const DWORD scan = key->scanCode;

    if (vk == VK_BACK) {
        engine.onBackspace();
        return CallNextHookEx(st.hook, code, wParam, lParam);
    }
    if (vk == VK_DELETE || vk == VK_LEFT || vk == VK_RIGHT || vk == VK_UP || vk == VK_DOWN
        || vk == VK_HOME || vk == VK_END || vk == VK_PRIOR || vk == VK_NEXT || vk == VK_ESCAPE) {
        engine.onReset();
        return CallNextHookEx(st.hook, code, wParam, lParam);
    }
    if (vk == VK_SPACE || vk == VK_RETURN || vk == VK_TAB) {
        const std::string boundary = vk == VK_SPACE ? " " : vk == VK_TAB ? "\t" : "\n";
        if (replaceLater(&st, engine.onBoundary(layout, boundary, boundary))) return 1;
        return CallNextHookEx(st.hook, code, wParam, lParam);
    }

    BYTE keys[256];
    fillState(keys, vk);
    const std::string cur = translateKey(vk, scan, keys, current);
    HKL otherHkl = layout == punto::CharMapping::Layout::English ? st.ru
                 : layout == punto::CharMapping::Layout::Russian ? st.en
                                                                 : nullptr;
    std::string oth;
    if (otherHkl) {
        const UINT vkOther = MapVirtualKeyExW(scan, MAPVK_VSC_TO_VK_EX, otherHkl);
        BYTE otherState[256];
        fillState(otherState, vkOther ? vkOther : vk);
        oth = translateKey(vkOther ? vkOther : vk, scan, otherState, otherHkl);
    }
    if (punto::LayoutSession::isWordText(cur) || punto::LayoutSession::isWordText(oth)) {
        engine.onWordChar(layout, cur, oth);
        return CallNextHookEx(st.hook, code, wParam, lParam);
    }
    if (cur.empty() && oth.empty()) return CallNextHookEx(st.hook, code, wParam, lParam);
    const std::string boundary = cur.empty() ? oth : cur;
    const std::string boundaryOther = oth.empty() ? boundary : oth;
    if (replaceLater(&st, engine.onBoundary(layout, boundary, boundaryOther))) return 1;
    return CallNextHookEx(st.hook, code, wParam, lParam);
}

} // namespace

void SwitchEngine::start() {
    command(QStringLiteral("RELOAD"));
    State& st = state();
    refreshLayouts(st);
    if (!st.hook)
        st.hook = SetWindowsHookExW(WH_KEYBOARD_LL, hookProc, GetModuleHandleW(nullptr), 0);
    running_ = st.hook != nullptr;
    log(running_ ? QStringLiteral("hook started") : QStringLiteral("hook failed"));
    setLayout(layoutOf(foregroundLayout()));

    static QTimer* timer = nullptr;
    if (!timer) {
        timer = new QTimer();
        QObject::connect(timer, &QTimer::timeout, []() {
            SwitchEngine::instance().setLayout(layoutOf(foregroundLayout()));
        });
        timer->start(500);
    }
}

void SwitchEngine::activateApp() {}
