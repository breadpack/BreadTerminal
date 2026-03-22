#include "termcore/serial_port.h"

#include <algorithm>
#include <cstring>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>
#endif

namespace termcore {

// ---------------------------------------------------------------------------
// listSerialPorts
// ---------------------------------------------------------------------------

#ifdef _WIN32

std::vector<std::string> listSerialPorts() {
    std::vector<std::string> ports;

    HKEY hKey = nullptr;
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE,
                      "HARDWARE\\DEVICEMAP\\SERIALCOMM",
                      0, KEY_READ, &hKey) != ERROR_SUCCESS) {
        return ports;
    }

    char valueName[256];
    char valueData[256];
    DWORD index = 0;

    for (;;) {
        DWORD nameLen = sizeof(valueName);
        DWORD dataLen = sizeof(valueData);
        DWORD type = 0;

        LONG rc = RegEnumValueA(hKey, index, valueName, &nameLen,
                                nullptr, &type,
                                reinterpret_cast<LPBYTE>(valueData), &dataLen);
        if (rc != ERROR_SUCCESS)
            break;

        if (type == REG_SZ) {
            ports.emplace_back(valueData, dataLen > 0 ? dataLen - 1 : 0);
        }
        ++index;
    }

    RegCloseKey(hKey);
    std::sort(ports.begin(), ports.end());
    return ports;
}

#else // Unix

static void scanDevDir(const char* prefix, std::vector<std::string>& out) {
    DIR* dir = opendir("/dev");
    if (!dir)
        return;
    size_t prefixLen = std::strlen(prefix);
    while (struct dirent* ent = readdir(dir)) {
        if (std::strncmp(ent->d_name, prefix, prefixLen) == 0) {
            out.push_back(std::string("/dev/") + ent->d_name);
        }
    }
    closedir(dir);
}

std::vector<std::string> listSerialPorts() {
    std::vector<std::string> ports;
    scanDevDir("ttyUSB", ports);
    scanDevDir("ttyACM", ports);
    scanDevDir("ttyS", ports);
#ifdef __APPLE__
    scanDevDir("cu.", ports);
#endif
    std::sort(ports.begin(), ports.end());
    return ports;
}

#endif

// ---------------------------------------------------------------------------
// SerialPortPty
// ---------------------------------------------------------------------------

SerialPortPty::SerialPortPty(const SerialConfig& config)
    : config_(config) {}

SerialPortPty::~SerialPortPty() {
    close();
}

const SerialConfig& SerialPortPty::config() const {
    return config_;
}

bool SerialPortPty::isOpen() const {
    return isOpen_;
}

// -- Pty interface ----------------------------------------------------------

bool SerialPortPty::spawn(const std::string& /*command*/,
                           const std::vector<std::string>& /*args*/,
                           const std::string& /*working_dir*/,
                           int /*rows*/, int /*cols*/,
                           const std::vector<std::pair<std::string, std::string>>& /*env_vars*/) {
    return open();
}

void SerialPortPty::resize(int /*rows*/, int /*cols*/) {
    // No-op for serial ports
}

bool SerialPortPty::isAlive() const {
    return isOpen_;
}

int SerialPortPty::pid() const {
    return -1; // No child process
}

int SerialPortPty::waitForExit() {
    // Block until the port is closed (return 0 immediately if already closed)
    return 0;
}

void SerialPortPty::signal(int /*sig*/) {
    // No child process to signal
}

// ---------------------------------------------------------------------------
// Platform: Windows
// ---------------------------------------------------------------------------

#ifdef _WIN32

bool SerialPortPty::open() {
    if (isOpen_)
        return true;

    std::string devicePath = "\\\\.\\" + config_.port;

    handle_ = CreateFileA(devicePath.c_str(),
                          GENERIC_READ | GENERIC_WRITE,
                          0, nullptr, OPEN_EXISTING,
                          FILE_ATTRIBUTE_NORMAL, nullptr);

    if (handle_ == INVALID_HANDLE_VALUE) {
        handle_ = nullptr;
        return false;
    }

    if (!applyConfig()) {
        CloseHandle(static_cast<HANDLE>(handle_));
        handle_ = nullptr;
        return false;
    }

    // Set timeouts for non-blocking behaviour
    COMMTIMEOUTS timeouts = {};
    timeouts.ReadIntervalTimeout = MAXDWORD;
    timeouts.ReadTotalTimeoutMultiplier = 0;
    timeouts.ReadTotalTimeoutConstant = 0;
    timeouts.WriteTotalTimeoutMultiplier = 0;
    timeouts.WriteTotalTimeoutConstant = 0;

    if (!SetCommTimeouts(static_cast<HANDLE>(handle_), &timeouts)) {
        CloseHandle(static_cast<HANDLE>(handle_));
        handle_ = nullptr;
        return false;
    }

    isOpen_ = true;
    return true;
}

bool SerialPortPty::applyConfig() {
    DCB dcb = {};
    dcb.DCBlength = sizeof(dcb);

    if (!GetCommState(static_cast<HANDLE>(handle_), &dcb))
        return false;

    dcb.BaudRate = static_cast<DWORD>(config_.baudRate);

    switch (config_.dataBits) {
    case SerialConfig::DataBits::Five:  dcb.ByteSize = 5; break;
    case SerialConfig::DataBits::Six:   dcb.ByteSize = 6; break;
    case SerialConfig::DataBits::Seven: dcb.ByteSize = 7; break;
    case SerialConfig::DataBits::Eight: dcb.ByteSize = 8; break;
    }

    switch (config_.parity) {
    case SerialConfig::Parity::None:  dcb.Parity = NOPARITY;    break;
    case SerialConfig::Parity::Odd:   dcb.Parity = ODDPARITY;   break;
    case SerialConfig::Parity::Even:  dcb.Parity = EVENPARITY;  break;
    case SerialConfig::Parity::Mark:  dcb.Parity = MARKPARITY;  break;
    case SerialConfig::Parity::Space: dcb.Parity = SPACEPARITY; break;
    }

    switch (config_.stopBits) {
    case SerialConfig::StopBits::One:          dcb.StopBits = ONESTOPBIT;   break;
    case SerialConfig::StopBits::OnePointFive: dcb.StopBits = ONE5STOPBITS; break;
    case SerialConfig::StopBits::Two:          dcb.StopBits = TWOSTOPBITS;  break;
    }

    switch (config_.flowControl) {
    case SerialConfig::FlowControl::None:
        dcb.fOutxCtsFlow = FALSE;
        dcb.fRtsControl = RTS_CONTROL_DISABLE;
        dcb.fOutX = FALSE;
        dcb.fInX = FALSE;
        break;
    case SerialConfig::FlowControl::Hardware:
        dcb.fOutxCtsFlow = TRUE;
        dcb.fRtsControl = RTS_CONTROL_HANDSHAKE;
        dcb.fOutX = FALSE;
        dcb.fInX = FALSE;
        break;
    case SerialConfig::FlowControl::Software:
        dcb.fOutxCtsFlow = FALSE;
        dcb.fRtsControl = RTS_CONTROL_DISABLE;
        dcb.fOutX = TRUE;
        dcb.fInX = TRUE;
        break;
    }

    return SetCommState(static_cast<HANDLE>(handle_), &dcb) != 0;
}

int SerialPortPty::read(char* buf, size_t buf_size) {
    if (!isOpen_ || !handle_)
        return -1;

    DWORD bytesRead = 0;
    if (!ReadFile(static_cast<HANDLE>(handle_), buf,
                  static_cast<DWORD>(buf_size), &bytesRead, nullptr)) {
        return -1;
    }
    return static_cast<int>(bytesRead);
}

int SerialPortPty::write(const char* data, size_t len) {
    if (!isOpen_ || !handle_)
        return -1;

    DWORD bytesWritten = 0;
    if (!WriteFile(static_cast<HANDLE>(handle_), data,
                   static_cast<DWORD>(len), &bytesWritten, nullptr)) {
        return -1;
    }
    return static_cast<int>(bytesWritten);
}

int SerialPortPty::fd() const {
    return -1; // Not applicable on Windows
}

void SerialPortPty::close() {
    if (handle_) {
        CloseHandle(static_cast<HANDLE>(handle_));
        handle_ = nullptr;
    }
    isOpen_ = false;
}

bool SerialPortPty::setBaudRate(uint32_t baud) {
    config_.baudRate = baud;
    if (!isOpen_ || !handle_)
        return true; // Will take effect on next open()

    return applyConfig();
}

#else // Unix

bool SerialPortPty::open() {
    if (isOpen_)
        return true;

    fd_ = ::open(config_.port.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (fd_ < 0)
        return false;

    if (!applyConfig()) {
        ::close(fd_);
        fd_ = -1;
        return false;
    }

    isOpen_ = true;
    return true;
}

bool SerialPortPty::applyConfig() {
    struct termios tty = {};
    if (tcgetattr(fd_, &tty) != 0)
        return false;

    // Baud rate
    speed_t speed = B115200;
    switch (config_.baudRate) {
    case 300:    speed = B300;    break;
    case 1200:   speed = B1200;   break;
    case 2400:   speed = B2400;   break;
    case 4800:   speed = B4800;   break;
    case 9600:   speed = B9600;   break;
    case 19200:  speed = B19200;  break;
    case 38400:  speed = B38400;  break;
    case 57600:  speed = B57600;  break;
    case 115200: speed = B115200; break;
    case 230400: speed = B230400; break;
#ifdef B460800
    case 460800: speed = B460800; break;
#endif
#ifdef B921600
    case 921600: speed = B921600; break;
#endif
    default:     speed = B115200; break;
    }
    cfsetispeed(&tty, speed);
    cfsetospeed(&tty, speed);

    // Data bits
    tty.c_cflag &= ~CSIZE;
    switch (config_.dataBits) {
    case SerialConfig::DataBits::Five:  tty.c_cflag |= CS5; break;
    case SerialConfig::DataBits::Six:   tty.c_cflag |= CS6; break;
    case SerialConfig::DataBits::Seven: tty.c_cflag |= CS7; break;
    case SerialConfig::DataBits::Eight: tty.c_cflag |= CS8; break;
    }

    // Parity
    switch (config_.parity) {
    case SerialConfig::Parity::None:
        tty.c_cflag &= ~PARENB;
        break;
    case SerialConfig::Parity::Odd:
        tty.c_cflag |= PARENB | PARODD;
        break;
    case SerialConfig::Parity::Even:
        tty.c_cflag |= PARENB;
        tty.c_cflag &= ~PARODD;
        break;
    case SerialConfig::Parity::Mark:
#ifdef CMSPAR
        tty.c_cflag |= PARENB | PARODD | CMSPAR;
#else
        tty.c_cflag |= PARENB | PARODD;
#endif
        break;
    case SerialConfig::Parity::Space:
#ifdef CMSPAR
        tty.c_cflag |= PARENB | CMSPAR;
        tty.c_cflag &= ~PARODD;
#else
        tty.c_cflag |= PARENB;
        tty.c_cflag &= ~PARODD;
#endif
        break;
    }

    // Stop bits
    switch (config_.stopBits) {
    case SerialConfig::StopBits::One:
        tty.c_cflag &= ~CSTOPB;
        break;
    case SerialConfig::StopBits::OnePointFive:
    case SerialConfig::StopBits::Two:
        tty.c_cflag |= CSTOPB;
        break;
    }

    // Flow control
    switch (config_.flowControl) {
    case SerialConfig::FlowControl::None:
        tty.c_cflag &= ~CRTSCTS;
        tty.c_iflag &= ~(IXON | IXOFF | IXANY);
        break;
    case SerialConfig::FlowControl::Hardware:
        tty.c_cflag |= CRTSCTS;
        tty.c_iflag &= ~(IXON | IXOFF | IXANY);
        break;
    case SerialConfig::FlowControl::Software:
        tty.c_cflag &= ~CRTSCTS;
        tty.c_iflag |= IXON | IXOFF;
        break;
    }

    // Raw mode
    tty.c_cflag |= CLOCAL | CREAD;
    tty.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG);
    tty.c_iflag &= ~(IGNBRK | BRKINT | ICRNL | INLCR | IGNCR);
    tty.c_oflag &= ~OPOST;

    // Non-blocking read: return immediately with whatever is available
    tty.c_cc[VMIN] = 0;
    tty.c_cc[VTIME] = 0;

    return tcsetattr(fd_, TCSANOW, &tty) == 0;
}

int SerialPortPty::read(char* buf, size_t buf_size) {
    if (!isOpen_ || fd_ < 0)
        return -1;

    ssize_t n = ::read(fd_, buf, buf_size);
    if (n < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK)
            return 0;
        return -1;
    }
    return static_cast<int>(n);
}

int SerialPortPty::write(const char* data, size_t len) {
    if (!isOpen_ || fd_ < 0)
        return -1;

    ssize_t n = ::write(fd_, data, len);
    if (n < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK)
            return 0;
        return -1;
    }
    return static_cast<int>(n);
}

int SerialPortPty::fd() const {
    return fd_;
}

void SerialPortPty::close() {
    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
    }
    isOpen_ = false;
}

bool SerialPortPty::setBaudRate(uint32_t baud) {
    config_.baudRate = baud;
    if (!isOpen_ || fd_ < 0)
        return true; // Will take effect on next open()

    return applyConfig();
}

#endif // _WIN32 / Unix

// ---------------------------------------------------------------------------
// parseSerialSpec
// ---------------------------------------------------------------------------

bool parseSerialSpec(const std::string& spec, SerialConfig& out) {
    // Format: "PORT:BAUD:CONFIG"
    // Examples: "COM3:9600:8N1", "/dev/ttyUSB0:115200:8N1", "COM3"
    // CONFIG is optional (defaults to 8N1), BAUD is optional (defaults to 115200)

    if (spec.empty())
        return false;

    out = SerialConfig{};

    // Find the port portion. On Windows, port names don't contain ':' normally,
    // but on Unix paths like /dev/ttyUSB0 also don't. We split on ':'.
    size_t first = spec.find(':');
    if (first == std::string::npos) {
        // Just a port name
        out.port = spec;
        return true;
    }

    out.port = spec.substr(0, first);
    if (out.port.empty())
        return false;

    // Parse baud rate
    size_t second = spec.find(':', first + 1);
    std::string baudStr;
    if (second == std::string::npos) {
        baudStr = spec.substr(first + 1);
    } else {
        baudStr = spec.substr(first + 1, second - first - 1);
    }

    if (!baudStr.empty()) {
        try {
            unsigned long baud = std::stoul(baudStr);
            out.baudRate = static_cast<uint32_t>(baud);
        } catch (...) {
            return false;
        }
    }

    // Parse config string (e.g., "8N1", "7E2")
    if (second != std::string::npos) {
        std::string cfgStr = spec.substr(second + 1);
        if (cfgStr.size() != 3)
            return false;

        // Data bits
        switch (cfgStr[0]) {
        case '5': out.dataBits = SerialConfig::DataBits::Five;  break;
        case '6': out.dataBits = SerialConfig::DataBits::Six;   break;
        case '7': out.dataBits = SerialConfig::DataBits::Seven; break;
        case '8': out.dataBits = SerialConfig::DataBits::Eight; break;
        default: return false;
        }

        // Parity
        switch (cfgStr[1]) {
        case 'N': case 'n': out.parity = SerialConfig::Parity::None;  break;
        case 'O': case 'o': out.parity = SerialConfig::Parity::Odd;   break;
        case 'E': case 'e': out.parity = SerialConfig::Parity::Even;  break;
        case 'M': case 'm': out.parity = SerialConfig::Parity::Mark;  break;
        case 'S': case 's': out.parity = SerialConfig::Parity::Space; break;
        default: return false;
        }

        // Stop bits
        switch (cfgStr[2]) {
        case '1': out.stopBits = SerialConfig::StopBits::One; break;
        case '2': out.stopBits = SerialConfig::StopBits::Two; break;
        default: return false;
        }
    }

    return true;
}

} // namespace termcore
