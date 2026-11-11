#include <gtest/gtest.h>

#include "../src/core/CharMapping.h"
#include "../src/core/LayoutSession.h"

using namespace punto;

namespace {

void typeWord(LayoutSession& session, const char* ascii) {
    for (const char* p = ascii; *p; ++p) session.noteChar(std::string(1, *p), {});
}

} // namespace

TEST(LayoutSession, EnglishGarbageBecomesRussian) {
    LayoutSession session;
    typeWord(session, "ghbdtn");
    const auto d = session.onBoundary(CharMapping::Layout::English, " ", " ");
    EXPECT_EQ(d.action, LayoutSession::Action::SwitchLayout);
    EXPECT_EQ(d.text, std::string("привет"));
    EXPECT_EQ(d.boundary, " ");
    EXPECT_EQ(d.deleteCount, 6);
    EXPECT_TRUE(session.empty());
}

TEST(LayoutSession, RealEnglishStays) {
    LayoutSession session;
    typeWord(session, "hello");
    const auto d = session.onBoundary(CharMapping::Layout::English, " ", " ");
    EXPECT_EQ(d.action, LayoutSession::Action::Keep);
    EXPECT_EQ(d.deleteCount, 0);
}

TEST(LayoutSession, DisabledDoesNotSwitch) {
    LayoutSession session;
    LayoutSession::Config cfg;
    cfg.enabled = false;
    cfg.typos = false;
    session.setConfig(cfg);
    typeWord(session, "ghbdtn");
    const auto d = session.onBoundary(CharMapping::Layout::English, " ", " ");
    EXPECT_EQ(d.action, LayoutSession::Action::Keep);
}

TEST(LayoutSession, BackspaceDropsLastLetter) {
    LayoutSession session;
    typeWord(session, "gh");
    session.noteBackspace();
    const auto d = session.onBoundary(CharMapping::Layout::English, " ", " ");
    EXPECT_EQ(d.action, LayoutSession::Action::Keep);
    EXPECT_EQ(d.deleteCount, 0);
}
