#pragma once

#include "CharMapping.h"

#include <string>

namespace punto {

// Word buffer shared by the macOS and Windows keyboard hooks.
// The Linux daemon keeps its own path. This one only decides what to do
// with a finished word; the platform deletes and retypes.
class LayoutSession {
public:
    struct Config {
        bool   enabled                         = true;
        bool   typos                           = true;
        int    minWordLength                   = 3;
        double confidenceThreshold             = 0.18;
        double minPlausibleDominantBigramScore = 0.42;
    };

    enum class Action { Keep, SwitchLayout, FixTypo };

    struct Decision {
        Action      action     = Action::Keep;
        std::string text;         // replacement for the word already on screen
        std::string boundary;     // separator to type after it, in the destination layout
        int         deleteCount = 0; // code points to erase (the word only)
    };

    void setConfig(const Config& cfg) { cfg_ = cfg; }
    const Config& config() const { return cfg_; }

    bool empty() const { return typed_.empty(); }
    void reset();
    void noteBackspace();

    // `other` is the same physical key in the other layout. Empty uses CharMapping.
    void noteChar(const std::string& typed, const std::string& other);

    // The boundary key has not been inserted yet. Always finishes the current word.
    Decision onBoundary(CharMapping::Layout current,
                        const std::string& boundaryTyped,
                        const std::string& boundaryOther);

    // A letter on either layout stays inside the word (JCUKEN comma is "б").
    static bool isWordText(const std::string& utf8);

private:
    Config      cfg_{};
    std::string typed_;
    std::string other_;
};

} // namespace punto
