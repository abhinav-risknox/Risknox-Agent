#pragma once

#include <string>

namespace ResolutePulse {

enum class ThreatAction {
    Quarantine,
    Ignore,
    Details,
    Dismissed,
    Unknown
};

struct ThreatNotification {
    std::string fileName;
    std::string filePath;
    std::string threatName;
    std::string sourceUrl;
    int         autoCloseSeconds = 10;
};

class ThreatNotifier {
public:
    // Path to Notification.ps1 — set once at startup
    void setScriptPath(const std::string& path) { scriptPath_ = path; }

    // Show the notification window and return the user's action.
    // Blocks until the user clicks or the timer expires.
    ThreatAction notify(const ThreatNotification& info) const;

    // Non-blocking: quarantine the file (move to quarantine dir)
    bool quarantine(const std::string& filePath,
                    const std::string& threatName,
                    const std::string& quarantineDir) const;

private:
    std::string scriptPath_;

    ThreatAction parseAction(const std::string& output) const;
};

} // namespace ResolutePulse
