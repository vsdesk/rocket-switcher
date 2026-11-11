#include "SwitchEngine.h"

#include "../core/TypoCorrector.h"
#include "../core/Utf8Utils.h"
#include "../core/WordDictionary.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSettings>
#include <QTextStream>

namespace {

QString configFile() {
    const QString home = QDir::homePath();
    const QString neu = home + QStringLiteral("/.config/rocket-switcher/daemon.ini");
    const QString old = home + QStringLiteral("/.config/punto-switcher/daemon.ini");
    if (QFileInfo::exists(neu) || !QFileInfo::exists(old)) return neu;
    return old;
}

bool parseBool(const QVariant& v, bool fallback) {
    if (!v.isValid()) return fallback;
    if (v.typeId() == QMetaType::Bool) return v.toBool();
    const QString s = v.toString().trimmed().toLower();
    if (s == QLatin1String("1") || s == QLatin1String("true") || s == QLatin1String("yes"))
        return true;
    if (s == QLatin1String("0") || s == QLatin1String("false") || s == QLatin1String("no"))
        return false;
    return fallback;
}

} // namespace

SwitchEngine& SwitchEngine::instance() {
    static SwitchEngine engine;
    return engine;
}

void SwitchEngine::log(const QString& line) {
    QFile file(QDir::tempPath() + QStringLiteral("/rocket-switcher.log"));
    if (!file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) return;
    QTextStream out(&file);
    out << line << '\n';
}

void SwitchEngine::applyConfig() {
    QSettings cfg(configFile(), QSettings::IniFormat);
    const bool passthrough = parseBool(cfg.value(QStringLiteral("daemon/passthrough")), false);
    const bool enabled = parseBool(cfg.value(QStringLiteral("autoswitch/enabled")), true);
    punto::LayoutSession::Config c;
    c.enabled = enabled && !passthrough;
    c.typos = parseBool(cfg.value(QStringLiteral("autoswitch/typo_correct")), true) && !passthrough;
    c.minWordLength = cfg.value(QStringLiteral("autoswitch/min_word_length"), 3).toInt();
    c.confidenceThreshold = cfg.value(QStringLiteral("autoswitch/confidence_threshold"), 0.18).toDouble();
    c.minPlausibleDominantBigramScore =
        cfg.value(QStringLiteral("autoswitch/min_plausible_dominant_bigram_score"), 0.42).toDouble();
    if (c.minWordLength < 1) c.minWordLength = 1;
    session_.setConfig(c);
    sound_ = parseBool(cfg.value(QStringLiteral("autoswitch/sound")), true);
}

void SwitchEngine::setEnabled(bool on) {
    QSettings cfg(configFile(), QSettings::IniFormat);
    cfg.setValue(QStringLiteral("autoswitch/enabled"), on);
    cfg.sync();
    applyConfig();
}

QString SwitchEngine::command(const QString& cmd) {
    const QString text = cmd.trimmed();
    if (text == QLatin1String("RELOAD")) {
        punto::WordDictionary::reload();
        punto::TypoCorrector::reload();
        applyConfig();
        const auto& dict = punto::WordDictionary::instance();
        log(QStringLiteral("reload en=%1 ru=%2 auto=%3")
                .arg(static_cast<qulonglong>(dict.englishSize()))
                .arg(static_cast<qulonglong>(dict.russianSize()))
                .arg(session_.config().enabled ? 1 : 0));
        return QStringLiteral("OK");
    }
    if (text == QLatin1String("AUTO 1") || text == QLatin1String("AUTO 0")) {
        setEnabled(text == QLatin1String("AUTO 1"));
        log(session_.config().enabled ? QStringLiteral("auto on") : QStringLiteral("auto off"));
        return QStringLiteral("OK");
    }
    if (text == QLatin1String("STATUS")) {
        if (!running_) return {};
        return session_.config().enabled ? QStringLiteral("AUTO 1") : QStringLiteral("AUTO 0");
    }
    return {};
}

QString SwitchEngine::layoutLabel() const {
    if (layout_ == punto::CharMapping::Layout::Russian) return QStringLiteral("Ру");
    if (layout_ == punto::CharMapping::Layout::English) return QStringLiteral("En");
    return QStringLiteral("Р");
}

void SwitchEngine::setLayout(punto::CharMapping::Layout layout) {
    layout_ = layout;
}

void SwitchEngine::onBackspace() {
    session_.noteBackspace();
}

void SwitchEngine::onReset() {
    session_.reset();
}

SwitchEngine::KeyOutcome SwitchEngine::fromDecision(const punto::LayoutSession::Decision& d,
                                                    punto::CharMapping::Layout layout) {
    KeyOutcome out;
    if (d.action == punto::LayoutSession::Action::Keep || d.deleteCount <= 0) return out;
    out.kind = KeyOutcome::Kind::Replace;
    out.insert = d.text + d.boundary;
    out.deleteCount = d.deleteCount;
    out.playSound = sound_;
    if (d.action == punto::LayoutSession::Action::SwitchLayout) {
        if (layout == punto::CharMapping::Layout::English) {
            out.switchToRussian = true;
            layout_ = punto::CharMapping::Layout::Russian;
        } else if (layout == punto::CharMapping::Layout::Russian) {
            out.switchToEnglish = true;
            layout_ = punto::CharMapping::Layout::English;
        }
    }
    log(QStringLiteral("replace '%1' delete=%2")
            .arg(QString::fromUtf8(out.insert.data(), static_cast<int>(out.insert.size())))
            .arg(out.deleteCount));
    return out;
}

SwitchEngine::KeyOutcome SwitchEngine::onWordChar(punto::CharMapping::Layout layout,
                                                  const std::string& typed,
                                                  const std::string& other) {
    setLayout(layout);
    session_.noteChar(typed, other);
    return {};
}

SwitchEngine::KeyOutcome SwitchEngine::onBoundary(punto::CharMapping::Layout layout,
                                                  const std::string& typed,
                                                  const std::string& other) {
    setLayout(layout);
    return fromDecision(session_.onBoundary(layout, typed, other), layout);
}
