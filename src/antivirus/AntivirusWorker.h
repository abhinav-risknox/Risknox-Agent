#pragma once

// AntivirusWorker.h - Entry point for rp-antivirus.exe
// Wraps clamscan.exe and freshclam.exe as child subprocesses,
// streams scan events back to the core agent via Named Pipe.

#include <string>

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
    // clamDir: directory containing clamscan.exe and the database/ folder
    explicit AntivirusWorker(const std::string& clamDir);

    // Run a scan on `path`. Emits JSON events to `pipe`.
    void runScan(const std::string& path,
                 class PipeServer& pipe);

    // Run freshclam to update virus definitions.
    // Returns true on success.
    bool updateDefinitions();

private:
    std::string clamDir_;  // e.g. "vendor/clamav"
};

} // namespace ResolutePulse

