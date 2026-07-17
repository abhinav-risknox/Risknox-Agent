#pragma once

#include <string>
#include <vector>
#include <filesystem>

namespace ResolutePulse {

class PathUtils {
public:
    /**
     * @return Path to %ProgramData%\Risknox Pulse
     */
    static std::filesystem::path getAgentDataDir();

    /**
     * @return Path to the Windows hosts file (usually C:\Windows\System32\drivers\etc\hosts)
     */
    static std::filesystem::path getHostsFilePath();

    /**
     * @return Path to standard Program Files directory
     */
    static std::filesystem::path getProgramFilesPath();

    /**
     * @return Path to the Windows system directory (e.g. C:\Windows\System32)
     */
    static std::filesystem::path getSystemDirectory();

    /**
     * @return Path to the directory where the current executable is located.
     *         Returns empty path on failure.
     */
    static std::filesystem::path getExecutableDir();

    /**
     * @return Downloads folder for the currently-running user via KNOWNFOLDERID.
     *         Returns empty path if the shell API fails.
     */
    static std::filesystem::path getUserDownloadsDir();

    /**
     * @return Downloads folder for every local user profile registered in
     *         HKLM\SOFTWARE\Microsoft\Windows NT\CurrentVersion\ProfileList.
     *         Only returns paths that exist on disk.
     */
    static std::vector<std::filesystem::path> getAllUsersDownloadsDirs();

    /**
     * Ensures the agent data directory exists
     */
    static void ensureDataDirExists();
};

} // namespace ResolutePulse
