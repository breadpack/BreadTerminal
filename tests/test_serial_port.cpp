#include <gtest/gtest.h>
#include "termcore/serial_port.h"

using namespace termcore;

// ---------------------------------------------------------------------------
// SerialConfig defaults
// ---------------------------------------------------------------------------

TEST(SerialConfigTest, DefaultValues) {
    SerialConfig cfg;
    EXPECT_TRUE(cfg.port.empty());
    EXPECT_EQ(cfg.baudRate, 115200u);
    EXPECT_EQ(cfg.dataBits, SerialConfig::DataBits::Eight);
    EXPECT_EQ(cfg.parity, SerialConfig::Parity::None);
    EXPECT_EQ(cfg.stopBits, SerialConfig::StopBits::One);
    EXPECT_EQ(cfg.flowControl, SerialConfig::FlowControl::None);
}

// ---------------------------------------------------------------------------
// parseSerialSpec
// ---------------------------------------------------------------------------

TEST(ParseSerialSpecTest, PortOnly) {
    SerialConfig cfg;
    EXPECT_TRUE(parseSerialSpec("COM3", cfg));
    EXPECT_EQ(cfg.port, "COM3");
    EXPECT_EQ(cfg.baudRate, 115200u);
    EXPECT_EQ(cfg.dataBits, SerialConfig::DataBits::Eight);
    EXPECT_EQ(cfg.parity, SerialConfig::Parity::None);
    EXPECT_EQ(cfg.stopBits, SerialConfig::StopBits::One);
}

TEST(ParseSerialSpecTest, UnixPortOnly) {
    SerialConfig cfg;
    EXPECT_TRUE(parseSerialSpec("/dev/ttyUSB0", cfg));
    EXPECT_EQ(cfg.port, "/dev/ttyUSB0");
    EXPECT_EQ(cfg.baudRate, 115200u);
}

TEST(ParseSerialSpecTest, PortAndBaud) {
    SerialConfig cfg;
    EXPECT_TRUE(parseSerialSpec("COM3:9600", cfg));
    EXPECT_EQ(cfg.port, "COM3");
    EXPECT_EQ(cfg.baudRate, 9600u);
    EXPECT_EQ(cfg.dataBits, SerialConfig::DataBits::Eight);
    EXPECT_EQ(cfg.parity, SerialConfig::Parity::None);
    EXPECT_EQ(cfg.stopBits, SerialConfig::StopBits::One);
}

TEST(ParseSerialSpecTest, FullSpec8N1) {
    SerialConfig cfg;
    EXPECT_TRUE(parseSerialSpec("COM3:9600:8N1", cfg));
    EXPECT_EQ(cfg.port, "COM3");
    EXPECT_EQ(cfg.baudRate, 9600u);
    EXPECT_EQ(cfg.dataBits, SerialConfig::DataBits::Eight);
    EXPECT_EQ(cfg.parity, SerialConfig::Parity::None);
    EXPECT_EQ(cfg.stopBits, SerialConfig::StopBits::One);
}

TEST(ParseSerialSpecTest, FullSpec7E2) {
    SerialConfig cfg;
    EXPECT_TRUE(parseSerialSpec("/dev/ttyS0:19200:7E2", cfg));
    EXPECT_EQ(cfg.port, "/dev/ttyS0");
    EXPECT_EQ(cfg.baudRate, 19200u);
    EXPECT_EQ(cfg.dataBits, SerialConfig::DataBits::Seven);
    EXPECT_EQ(cfg.parity, SerialConfig::Parity::Even);
    EXPECT_EQ(cfg.stopBits, SerialConfig::StopBits::Two);
}

TEST(ParseSerialSpecTest, OddParity) {
    SerialConfig cfg;
    EXPECT_TRUE(parseSerialSpec("COM1:115200:8O1", cfg));
    EXPECT_EQ(cfg.parity, SerialConfig::Parity::Odd);
}

TEST(ParseSerialSpecTest, MarkParity) {
    SerialConfig cfg;
    EXPECT_TRUE(parseSerialSpec("COM1:115200:8M1", cfg));
    EXPECT_EQ(cfg.parity, SerialConfig::Parity::Mark);
}

TEST(ParseSerialSpecTest, SpaceParity) {
    SerialConfig cfg;
    EXPECT_TRUE(parseSerialSpec("COM1:115200:8S1", cfg));
    EXPECT_EQ(cfg.parity, SerialConfig::Parity::Space);
}

TEST(ParseSerialSpecTest, LowercaseParity) {
    SerialConfig cfg;
    EXPECT_TRUE(parseSerialSpec("COM1:9600:8n1", cfg));
    EXPECT_EQ(cfg.parity, SerialConfig::Parity::None);
}

TEST(ParseSerialSpecTest, FiveDataBits) {
    SerialConfig cfg;
    EXPECT_TRUE(parseSerialSpec("COM1:9600:5N1", cfg));
    EXPECT_EQ(cfg.dataBits, SerialConfig::DataBits::Five);
}

TEST(ParseSerialSpecTest, SixDataBits) {
    SerialConfig cfg;
    EXPECT_TRUE(parseSerialSpec("COM1:9600:6N1", cfg));
    EXPECT_EQ(cfg.dataBits, SerialConfig::DataBits::Six);
}

// ---------------------------------------------------------------------------
// parseSerialSpec — invalid inputs
// ---------------------------------------------------------------------------

TEST(ParseSerialSpecTest, EmptyString) {
    SerialConfig cfg;
    EXPECT_FALSE(parseSerialSpec("", cfg));
}

TEST(ParseSerialSpecTest, InvalidBaud) {
    SerialConfig cfg;
    EXPECT_FALSE(parseSerialSpec("COM3:abc:8N1", cfg));
}

TEST(ParseSerialSpecTest, InvalidConfigLength) {
    SerialConfig cfg;
    EXPECT_FALSE(parseSerialSpec("COM3:9600:8N", cfg));
}

TEST(ParseSerialSpecTest, InvalidDataBits) {
    SerialConfig cfg;
    EXPECT_FALSE(parseSerialSpec("COM3:9600:3N1", cfg));
}

TEST(ParseSerialSpecTest, InvalidParity) {
    SerialConfig cfg;
    EXPECT_FALSE(parseSerialSpec("COM3:9600:8X1", cfg));
}

TEST(ParseSerialSpecTest, InvalidStopBits) {
    SerialConfig cfg;
    EXPECT_FALSE(parseSerialSpec("COM3:9600:8N3", cfg));
}

// ---------------------------------------------------------------------------
// listSerialPorts
// ---------------------------------------------------------------------------

TEST(ListSerialPortsTest, ReturnsVector) {
    // We can't guarantee any ports exist, but the function should not crash
    auto ports = listSerialPorts();
    // Just verify it returns without error; vector may be empty
    EXPECT_GE(ports.size(), 0u);
}

// ---------------------------------------------------------------------------
// SerialPortPty construction
// ---------------------------------------------------------------------------

TEST(SerialPortPtyTest, ConstructionDoesNotCrash) {
    SerialConfig cfg;
    cfg.port = "COM99"; // Non-existent port
    SerialPortPty pty(cfg);
    EXPECT_FALSE(pty.isOpen());
    EXPECT_FALSE(pty.isAlive());
    EXPECT_EQ(pty.pid(), -1);
    EXPECT_EQ(pty.config().port, "COM99");
    EXPECT_EQ(pty.config().baudRate, 115200u);
}

TEST(SerialPortPtyTest, OpenFailsOnBadPort) {
    SerialConfig cfg;
    cfg.port = "COM99"; // Non-existent
    SerialPortPty pty(cfg);
    EXPECT_FALSE(pty.open());
    EXPECT_FALSE(pty.isOpen());
}

TEST(SerialPortPtyTest, SpawnCallsOpen) {
    SerialConfig cfg;
    cfg.port = "COM99"; // Non-existent
    SerialPortPty pty(cfg);
    // spawn() delegates to open(), which should fail on a bad port
    EXPECT_FALSE(pty.spawn());
    EXPECT_FALSE(pty.isAlive());
}

TEST(SerialPortPtyTest, ReadWriteFailWhenClosed) {
    SerialConfig cfg;
    cfg.port = "COM99";
    SerialPortPty pty(cfg);
    char buf[64];
    EXPECT_EQ(pty.read(buf, sizeof(buf)), -1);
    EXPECT_EQ(pty.write("hello", 5), -1);
}

TEST(SerialPortPtyTest, ResizeIsNoOp) {
    SerialConfig cfg;
    cfg.port = "COM99";
    SerialPortPty pty(cfg);
    // Should not crash
    pty.resize(40, 120);
}

TEST(SerialPortPtyTest, SetBaudRateBeforeOpen) {
    SerialConfig cfg;
    cfg.port = "COM99";
    cfg.baudRate = 9600;
    SerialPortPty pty(cfg);
    EXPECT_TRUE(pty.setBaudRate(115200));
    EXPECT_EQ(pty.config().baudRate, 115200u);
}

TEST(SerialPortPtyTest, CloseIsIdempotent) {
    SerialConfig cfg;
    cfg.port = "COM99";
    SerialPortPty pty(cfg);
    // Closing an already-closed port should not crash
    pty.close();
    pty.close();
    EXPECT_FALSE(pty.isOpen());
}
