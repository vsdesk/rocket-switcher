#pragma once

#include "../core/CharMapping.h"
#include "../core/LayoutSession.h"

#include <QString>

#include <string>

// In-process keyboard switcher for macOS and Windows. Linux keeps the evdev daemon.
class SwitchEngine {
public:
    struct KeyOutcome {
        enum class Kind { Pass, Replace } kind = Kind::Pass;
        std::string insert;
        int         deleteCount      = 0;
        bool        switchToRussian  = false;
        bool        switchToEnglish  = false;
        bool        playSound        = false;
    };

    static SwitchEngine& instance();

    void start();
    bool isRunning() const { return running_; }
    QString command(const QString& cmd);
    QString layoutLabel() const;
    void activateApp();

    void setLayout(punto::CharMapping::Layout layout);
    void onBackspace();
    void onReset();
    KeyOutcome onWordChar(punto::CharMapping::Layout layout,
                          const std::string& typed,
                          const std::string& other);
    KeyOutcome onBoundary(punto::CharMapping::Layout layout,
                          const std::string& typed,
                          const std::string& other);

    static void log(const QString& line);

private:
    void applyConfig();
    void setEnabled(bool on);
    KeyOutcome fromDecision(const punto::LayoutSession::Decision& d,
                            punto::CharMapping::Layout layout);

    bool                          running_ = false;
    bool                          sound_   = true;
    punto::CharMapping::Layout    layout_  = punto::CharMapping::Layout::Unknown;
    punto::LayoutSession          session_;
};
