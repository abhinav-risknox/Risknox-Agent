#pragma once

#include <string>

namespace ResolutePulse {

enum class ThreatAction {
    Quarantine,    // Exit code 0
    Ignore,        // Exit code 1
    Details,       // Exit code 2
    Dismissed,     // Exit code 3
    AutoQuarantine,// Exit code 4
    Unknown
};

struct ThreatNotification {
    std::string fileName;
    std::string filePath;
    std::string threatName;
    std::string sourceUrl;
    std::string severity;       // critical | high | medium | low
    std::string hash;           // SHA256 (optional)
    int         autoCloseSeconds = 10;
};

class ThreatNotifier {
public:
    // Path to ThreatNotification.exe — set once at startup
    void setExePath(const std::string& path) { exePath_ = path; }

    // Show the notification window and return the user's action.
    // Blocks until the user clicks or the timer expires.
    ThreatAction notify(const ThreatNotification& info) const;

    // Non-blocking: quarantine the file (move to quarantine dir)
    bool quarantine(const std::string& filePath,
                    const std::string& threatName,
                    const std::string& quarantineDir) const;

private:
    std::string exePath_;   // ThreatNotification.exe

    ThreatAction parseExitCode(unsigned long exitCode) const;
};

} // namespace ResolutePulse
