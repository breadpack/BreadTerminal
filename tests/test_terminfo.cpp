#include <gtest/gtest.h>
#include "termcore/terminfo.h"

#include <cstring>
#include <string>

TEST(Terminfo, TermNameIsCorrect) {
    const char* name = termcore::breadTerminalTermName();
    ASSERT_NE(name, nullptr);
    EXPECT_STREQ(name, "xterm-breadterminal");
}

TEST(Terminfo, SourceIsNonEmpty) {
    const char* source = termcore::breadTerminalTerminfoSource();
    ASSERT_NE(source, nullptr);
    EXPECT_GT(std::strlen(source), 100u);
}

TEST(Terminfo, SourceContainsCursorMovement) {
    std::string source = termcore::breadTerminalTerminfoSource();
    EXPECT_NE(source.find("cup="), std::string::npos);
    EXPECT_NE(source.find("cuu="), std::string::npos);
    EXPECT_NE(source.find("cud="), std::string::npos);
    EXPECT_NE(source.find("cuf="), std::string::npos);
    EXPECT_NE(source.find("cub="), std::string::npos);
}

TEST(Terminfo, SourceContainsColorSupport) {
    std::string source = termcore::breadTerminalTerminfoSource();
    EXPECT_NE(source.find("colors#256"), std::string::npos);
    EXPECT_NE(source.find("setaf="), std::string::npos);
    EXPECT_NE(source.find("setab="), std::string::npos);
    EXPECT_NE(source.find("op="), std::string::npos);
}

TEST(Terminfo, SourceContainsSGR) {
    std::string source = termcore::breadTerminalTerminfoSource();
    EXPECT_NE(source.find("bold="), std::string::npos);
    EXPECT_NE(source.find("dim="), std::string::npos);
    EXPECT_NE(source.find("smul="), std::string::npos);
    EXPECT_NE(source.find("sgr0="), std::string::npos);
    EXPECT_NE(source.find("sitm="), std::string::npos);
    EXPECT_NE(source.find("ritm="), std::string::npos);
}

TEST(Terminfo, SourceContainsFunctionKeys) {
    std::string source = termcore::breadTerminalTerminfoSource();
    EXPECT_NE(source.find("kf1="), std::string::npos);
    EXPECT_NE(source.find("kf12="), std::string::npos);
    EXPECT_NE(source.find("kf24="), std::string::npos);
}

TEST(Terminfo, SourceContainsMouseSupport) {
    std::string source = termcore::breadTerminalTerminfoSource();
    EXPECT_NE(source.find("kmous="), std::string::npos);
    EXPECT_NE(source.find("XM="), std::string::npos);
}

TEST(Terminfo, SourceContainsBracketedPaste) {
    std::string source = termcore::breadTerminalTerminfoSource();
    EXPECT_NE(source.find("BE="), std::string::npos);
    EXPECT_NE(source.find("BD="), std::string::npos);
    EXPECT_NE(source.find("PS="), std::string::npos);
    EXPECT_NE(source.find("PE="), std::string::npos);
}

TEST(Terminfo, SourceContainsExtendedCaps) {
    std::string source = termcore::breadTerminalTerminfoSource();
    EXPECT_NE(source.find("Tc"), std::string::npos);
    EXPECT_NE(source.find("RGB"), std::string::npos);
    EXPECT_NE(source.find("Sync="), std::string::npos);
}

TEST(Terminfo, SourceContainsCursorStyle) {
    std::string source = termcore::breadTerminalTerminfoSource();
    EXPECT_NE(source.find("Ss="), std::string::npos);
    EXPECT_NE(source.find("Se="), std::string::npos);
}

TEST(Terminfo, SourceContainsAlternateScreen) {
    std::string source = termcore::breadTerminalTerminfoSource();
    EXPECT_NE(source.find("smcup="), std::string::npos);
    EXPECT_NE(source.find("rmcup="), std::string::npos);
}

TEST(Terminfo, InstallTerminfoRuns) {
    // This test verifies that installTerminfo() runs without crashing
    // and returns a valid result (either success with our term or fallback)
    auto result = termcore::installTerminfo();
    EXPECT_FALSE(result.term_name.empty());
    // Should be either our custom term or the fallback
    EXPECT_TRUE(result.term_name == "xterm-breadterminal" ||
                result.term_name == "xterm-256color");
}
