#pragma once

#include <string>
#include <Windows.h>

namespace ResolutePulse {
namespace ProcessUtils {

// Launches a process in the active interactive user session (Session 1+)
// instead of Session 0 (where services run).
// Essential for launching UI popups from the background service.
// Returns true if successful. If outPi is provided, it will be populated,
// and the caller is responsible for calling CloseHandle on hProcess/hThread.
bool launchInteractiveProcess(const std::string& cmdLine, PROCESS_INFORMATION* outPi = nullptr);

} // namespace ProcessUtils
} // namespace ResolutePulse
