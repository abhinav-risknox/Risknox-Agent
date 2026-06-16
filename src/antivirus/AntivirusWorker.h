#pragma once

// AntivirusWorker.h - Entry point for rp-antivirus.exe
// Wraps clamscan.exe and freshclam.exe as child subprocesses,
// streams scan events back to the core agent via Named Pipe.

#include "notification/ThreatNotifier.h"
#include <string>
#include <vector>
#include <nlohmann/json.hpp>

namespace ResolutePulse {

// Runs a ClamAV scan by spawning clamscan.exe as a child process.
// Events are streamed to the provided Named Pipe server:
//   {"type":"progress","filesScanned":N}
//   {"type":"threat","file":"C:\\bad.exe","threat":"Win.Malware.Agent"}
//   {"type":"complete","filesScanned":N,"threats":N}
//
// Call from the rp-antivirus.exe main() loop.
class AntivirusWorker {
public:
    // binDir: directory containing clamscan.exe and freshclam.exe
    // dbDir:  directory containing the virus definitions (database/)
    AntivirusWorker(const std::string& binDir, const std::string& dbDir);

    // Run a scan on `path`. Emits JSON events to `pipe`.
    // If notifier is set, shows a threat popup when a threat is found.
    void runScan(const std::string& path,
                 class PipeServer& pipe,
                 ThreatNotifier* notifier = nullptr,
                 const std::string& quarantineDir = "");

    // Run a scan on multiple paths. Emits JSON events to `pipe`.
    void runScan(const std::vector<std::string>& paths,
                 PipeServer& pipe,
                 ThreatNotifier* notifier = nullptr,
                 const std::string& quarantineDir = "");

    // Run freshclam to update virus definitions.
    // Returns true on success.
    bool updateDefinitions();

    // Inspect ClamAV database files and return metadata summary.
    nlohmann::json getDatabaseInfo() const;

private:
    std::string binDir_;
    std::string dbDir_;
};

} // namespace ResolutePulse

