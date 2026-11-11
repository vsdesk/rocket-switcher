#include "LayoutSession.h"

#include "AutoSwitchHeuristic.h"
#include "TypoCorrector.h"
#include "Utf8Utils.h"

namespace punto {

namespace {

std::string swappedChar(const std::string& utf8) {
    const std::u32string in = utf8_to_utf32(utf8);
    std::u32string out;
    out.reserve(in.size());
    for (char32_t c : in) out.push_back(CharMapping::swapChar(c));
    return utf32_to_utf8(out);
}

std::string popLast(const std::string& utf8) {
    std::u32string cps = utf8_to_utf32(utf8);
    if (!cps.empty()) cps.pop_back();
    return utf32_to_utf8(cps);
}

} // namespace

bool LayoutSession::isWordText(const std::string& utf8) {
    const std::u32string cps = utf8_to_utf32(utf8);
    if (cps.empty()) return false;
    return CharMapping::isWordChar(cps[0]);
}

void LayoutSession::reset() {
    typed_.clear();
    other_.clear();
}

void LayoutSession::noteBackspace() {
    typed_ = popLast(typed_);
    other_ = popLast(other_);
}

void LayoutSession::noteChar(const std::string& typed, const std::string& other) {
    std::string t = typed.empty() ? swappedChar(other) : typed;
    std::string o = other.empty() ? swappedChar(t) : other;
    if (t.empty()) return;
    if (utf8_length(typed_) > 64) reset();
    typed_ += t;
    other_ += o;
}

LayoutSession::Decision LayoutSession::onBoundary(CharMapping::Layout current,
                                                  const std::string& boundaryTyped,
                                                  const std::string& boundaryOther) {
    Decision d;
    d.boundary = boundaryTyped;
    const std::string typed = typed_;
    const std::string other = other_;
    reset();
    if (typed.empty()) return d;

    const bool known = current == CharMapping::Layout::English
                       || current == CharMapping::Layout::Russian;
    if (cfg_.enabled && known) {
        HeuristicConfig h;
        h.enabled                         = true;
        h.minWordLength                   = cfg_.minWordLength;
        h.confidenceThreshold             = cfg_.confidenceThreshold;
        h.minPlausibleDominantBigramScore = cfg_.minPlausibleDominantBigramScore;
        if (AutoSwitchHeuristic(h).shouldSwitch(typed, current, other) && other != typed) {
            d.action      = Action::SwitchLayout;
            d.text        = other;
            d.boundary    = boundaryOther.empty() ? boundaryTyped : boundaryOther;
            d.deleteCount = static_cast<int>(utf8_length(typed));
            return d;
        }
    }

    if (cfg_.typos && known) {
        const std::string fixed = TypoCorrector::instance().correct(utf8_to_utf32(typed), current);
        if (!fixed.empty() && fixed != typed) {
            d.action      = Action::FixTypo;
            d.text        = fixed;
            d.boundary    = boundaryTyped;
            d.deleteCount = static_cast<int>(utf8_length(typed));
            return d;
        }
    }
    return d;
}

} // namespace punto
