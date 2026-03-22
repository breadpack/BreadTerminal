#ifndef TERMCORE_SERIAL_PORT_H
#define TERMCORE_SERIAL_PORT_H

#include "termcore/pty.h"
#include <cstdint>
#include <string>
#include <vector>

namespace termcore {

/// Serial port configuration
struct SerialConfig {
    std::string port;          // e.g., "COM3" or "/dev/ttyUSB0"
    uint32_t baudRate = 115200;

    enum class DataBits { Five = 5, Six = 6, Seven = 7, Eight = 8 };
    DataBits dataBits = DataBits::Eight;

    enum class Parity { None, Odd, Even, Mark, Space };
    Parity parity = Parity::None;

    enum class StopBits { One, OnePointFive, Two };
    StopBits stopBits = StopBits::One;

    enum class FlowControl { None, Hardware, Software };
    FlowControl flowControl = FlowControl::None;
};

/// List available serial ports on the system
std::vector<std::string> listSerialPorts();

/// Serial port connection as a Pty-compatible interface.
/// This allows the terminal to treat serial ports like any other connection.
class SerialPortPty : public Pty {
public:
    explicit SerialPortPty(const SerialConfig& config);
    ~SerialPortPty() override;

    // Pty interface
    bool spawn(const std::string& command = "",
               const std::vector<std::string>& args = {},
               const std::string& working_dir = "",
               int rows = 24, int cols = 80,
               const std::vector<std::pair<std::string, std::string>>& env_vars = {}) override;
    int read(char* buf, size_t buf_size) override;
    int write(const char* data, size_t len) override;
    void resize(int rows, int cols) override;
    bool isAlive() const override;
    int pid() const override;
    int fd() const override;
    int waitForExit() override;
    void signal(int sig) override;

    // Serial-specific
    bool open();
    bool isOpen() const;
    const SerialConfig& config() const;

    /// Change baud rate on the fly
    bool setBaudRate(uint32_t baud);

private:
    SerialConfig config_;
    bool isOpen_ = false;

#ifdef _WIN32
    void* handle_ = nullptr;  // HANDLE
#else
    int fd_ = -1;
#endif

    bool applyConfig();
};

/// Parse a serial port string like "COM3:115200:8N1"
/// Returns true if parsed successfully.
bool parseSerialSpec(const std::string& spec, SerialConfig& out);

} // namespace termcore

#endif // TERMCORE_SERIAL_PORT_H
