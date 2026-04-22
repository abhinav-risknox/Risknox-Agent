#include "PatchManager.h"
#include "utils/Logger.h"

#include <Windows.h>
#include <wuapi.h>
#include <chrono>
#include <sstream>
#include <algorithm>

#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "oleaut32.lib")

namespace {
    std::string bstrToString(BSTR bstr) {
        if (!bstr) return "";
        int len = SysStringLen(bstr);
        if (len == 0) return "";
        int size_needed = WideCharToMultiByte(CP_UTF8, 0, bstr, len, NULL, 0, NULL, NULL);
        std::string strTo(size_needed, 0);
        WideCharToMultiByte(CP_UTF8, 0, bstr, len, &strTo[0], size_needed, NULL, NULL);
        return strTo;
    }

    // MinGW often misses these specific WUA GUIDs
    static const GUID CLSID_UpdateSession_Impl = 
        {0x4CB43D7F, 0x7EEE, 0x4906, {0x86, 0x98, 0x60, 0xDA, 0x1C, 0x38, 0xF2, 0xFE}};
    static const GUID IID_IUpdateSession_Impl = 
        {0x816858A4, 0x260D, 0x4260, {0x93, 0x3A, 0x25, 0x85, 0xF1, 0xAB, 0xC7, 0x6B}};
    static const GUID IID_IUpdateCollection_Impl = 
        {0x07F7438C, 0x7709, 0x485A, {0x8C, 0xE0, 0x2C, 0x12, 0x05, 0x31, 0x56, 0x27}};
    static const CLSID CLSID_UpdateColl_Impl = 
        {0x13639463, 0x00DB, 0x4646, {0x80, 0x3D, 0x52, 0x80, 0x26, 0x14, 0x0D, 0x88}};
}

namespace ResolutePulse {

PatchManager::PatchManager() = default;

PatchManager::~PatchManager() {
    stop();
}

bool PatchManager::initialize(const PatchConfig& config) {
    config_ = config;
    LOG_INFO("PatchManager initialized (autoScan={}, interval={}h, autoInstall={})",
             config_.autoScan, config_.scanIntervalHours, config_.autoInstall);
    return true;
}

bool PatchManager::start() {
    if (running_.load()) return true;
    running_ = true;

    if (config_.autoScan && config_.scanIntervalHours > 0) {
        scanThread_ = std::thread(&PatchManager::scanLoop, this);
        LOG_INFO("PatchManager scan loop started ({}h interval)", config_.scanIntervalHours);
    }
    return true;
}

void PatchManager::stop() {
    running_ = false;
    if (scanThread_.joinable()) {
        scanThread_.join();
    }
}

std::string PatchManager::getCurrentTimestamp() const {
    time_t now = time(nullptr);
    char buf[64];
    strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", gmtime(&now));
    return std::string(buf);
}

std::vector<PatchInfo> PatchManager::scanForUpdates() {
    LOG_INFO("Scanning for Windows updates...");
    std::vector<PatchInfo> pending;

    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    bool comInitialized = SUCCEEDED(hr) || hr == RPC_E_CHANGED_MODE;

    if (!comInitialized) {
        LOG_ERROR("PatchManager: COM initialization failed: 0x{:08X}", (unsigned)hr);
        return pending;
    }

    IUpdateSession* session = nullptr;
    hr = CoCreateInstance(
        CLSID_UpdateSession_Impl, nullptr, CLSCTX_INPROC_SERVER,
        IID_IUpdateSession_Impl, reinterpret_cast<void**>(&session));

    if (FAILED(hr) || !session) {
        LOG_ERROR("PatchManager: Failed to create UpdateSession: 0x{:08X}", (unsigned)hr);
        CoUninitialize();
        return pending;
    }

    IUpdateSearcher* searcher = nullptr;
    hr = session->CreateUpdateSearcher(&searcher);
    if (FAILED(hr) || !searcher) {
        LOG_ERROR("PatchManager: Failed to create UpdateSearcher: 0x{:08X}", (unsigned)hr);
        session->Release();
        CoUninitialize();
        return pending;
    }

    // Search for pending (not installed) updates, excluding drivers
    ISearchResult* result = nullptr;
    BSTR criteria = SysAllocString(L"IsInstalled=0 AND Type='Software'");
    hr = searcher->Search(criteria, &result);
    SysFreeString(criteria);

    if (FAILED(hr) || !result) {
        LOG_WARN("PatchManager: Update search failed: 0x{:08X}", (unsigned)hr);
        searcher->Release();
        session->Release();
        CoUninitialize();
        return pending;
    }

    IUpdateCollection* updates = nullptr;
    result->get_Updates(&updates);

    if (updates) {
        LONG count = 0;
        updates->get_Count(&count);
        LOG_INFO("PatchManager: Found {} pending updates", count);

        for (LONG i = 0; i < count; ++i) {
            IUpdate* update = nullptr;
            updates->get_Item(i, &update);
            if (!update) continue;

            PatchInfo info;
            info.isInstalled = false;

            // Title
            BSTR title = nullptr;
            update->get_Title(&title);
            if (title) {
                info.title = bstrToString(title);
            }

            // Description
            BSTR desc = nullptr;
            update->get_Description(&desc);
            if (desc) {
                info.description = bstrToString(desc);
            }

            // KB article IDs
            IStringCollection* kbIds = nullptr;
            update->get_KBArticleIDs(&kbIds);
            if (kbIds) {
                LONG kbCount = 0;
                kbIds->get_Count(&kbCount);
                std::string kbList;
                for (LONG k = 0; k < kbCount; ++k) {
                    BSTR kbStr = nullptr;
                    kbIds->get_Item(k, &kbStr);
                    if (kbStr) {
                        if (!kbList.empty()) kbList += ", ";
                        kbList += "KB";
                        kbList += bstrToString(kbStr);
                    }
                }
                info.kb = kbList.empty() ? "N/A" : kbList;
                kbIds->Release();
            }

            // Update ID
            IUpdateIdentity* identity = nullptr;
            update->get_Identity(&identity);
            if (identity) {
                BSTR updateId = nullptr;
                identity->get_UpdateID(&updateId);
                if (updateId) {
                    info.updateId = bstrToString(updateId);
                }
                identity->Release();
            }

            // Severity (via MsrcSeverity)
            BSTR severity = nullptr;
            update->get_MsrcSeverity(&severity);
            if (severity) {
                info.severity = bstrToString(severity);
            }
            if (info.severity.empty()) info.severity = "Unspecified";

            // Categories
            ICategoryCollection* cats = nullptr;
            update->get_Categories(&cats);
            if (cats) {
                LONG catCount = 0;
                cats->get_Count(&catCount);
                std::string catList;
                for (LONG c = 0; c < catCount; ++c) {
                    ICategory* cat = nullptr;
                    cats->get_Item(c, &cat);
                    if (cat) {
                        BSTR catName = nullptr;
                        cat->get_Name(&catName);
                        if (catName) {
                            std::string catStr = bstrToString(catName);
                            if (catStr != "Drivers") {
                                if (!catList.empty()) catList += ", ";
                                catList += catStr;
                            }
                        }
                        cat->Release();
                    }
                }
                info.categories = catList;
                cats->Release();
            }

            // Check exclude list
            bool excluded = false;
            for (const auto& exKb : config_.excludeKBs) {
                if (info.kb.find(exKb) != std::string::npos) {
                    excluded = true;
                    break;
                }
            }

            if (!excluded) {
                pending.push_back(std::move(info));
            }

            update->Release();
        }
        updates->Release();
    }

    result->Release();
    searcher->Release();
    session->Release();
    CoUninitialize();

    // Cache results
    {
        std::lock_guard<std::mutex> lock(mutex_);
        pendingUpdates_ = pending;
        lastScanTime_ = getCurrentTimestamp();
        lastScanPendingCount_ = static_cast<int>(pending.size());
    }

    LOG_INFO("PatchManager: Scan complete - {} pending updates", pending.size());
    for (const auto& p : pending) {
        LOG_INFO("  Update: {} | KB={} | ID={} | Severity={}", p.title, p.kb, p.updateId, p.severity);
    }

    // Fire event callback
    if (eventCallback_) {
        nlohmann::json event;
        event["type"] = "patch_scan_complete";
        event["timestamp"] = getCurrentTimestamp();
        event["pendingCount"] = pending.size();
        nlohmann::json patchList = nlohmann::json::array();
        for (const auto& p : pending) {
            patchList.push_back(p.toJson());
        }
        event["patches"] = patchList;
        eventCallback_(event);
    }

    return pending;
}

std::vector<PatchInfo> PatchManager::getInstalledUpdates() {
    LOG_INFO("Querying installed updates...");
    std::vector<PatchInfo> installed;

    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    bool comInitialized = SUCCEEDED(hr) || hr == RPC_E_CHANGED_MODE;
    if (!comInitialized) return installed;

    IUpdateSession* session = nullptr;
    hr = CoCreateInstance(CLSID_UpdateSession_Impl, nullptr, CLSCTX_INPROC_SERVER,
                          IID_IUpdateSession_Impl, reinterpret_cast<void**>(&session));
    if (FAILED(hr) || !session) { CoUninitialize(); return installed; }

    IUpdateSearcher* searcher = nullptr;
    session->CreateUpdateSearcher(&searcher);
    if (!searcher) { session->Release(); CoUninitialize(); return installed; }

    ISearchResult* result = nullptr;
    BSTR criteria = SysAllocString(L"IsInstalled=1 AND Type='Software'");
    hr = searcher->Search(criteria, &result);
    SysFreeString(criteria);

    if (SUCCEEDED(hr) && result) {
        IUpdateCollection* updates = nullptr;
        result->get_Updates(&updates);
        if (updates) {
            LONG count = 0;
            updates->get_Count(&count);
            for (LONG i = 0; i < count; ++i) {
                IUpdate* update = nullptr;
                updates->get_Item(i, &update);
                if (!update) continue;

                PatchInfo info;
                info.isInstalled = true;

                BSTR title = nullptr;
                update->get_Title(&title);
                if (title) { info.title = bstrToString(title); }

                IStringCollection* kbIds = nullptr;
                update->get_KBArticleIDs(&kbIds);
                if (kbIds) {
                    LONG kbCount = 0;
                    kbIds->get_Count(&kbCount);
                    std::string kbList;
                    for (LONG k = 0; k < kbCount; ++k) {
                        BSTR kbStr = nullptr;
                        kbIds->get_Item(k, &kbStr);
                        if (kbStr) {
                            if (!kbList.empty()) kbList += ", ";
                            kbList += "KB";
                            kbList += bstrToString(kbStr);
                        }
                    }
                    info.kb = kbList.empty() ? "N/A" : kbList;
                    kbIds->Release();
                }

                IUpdateIdentity* identity = nullptr;
                update->get_Identity(&identity);
                if (identity) {
                    BSTR uid = nullptr;
                    identity->get_UpdateID(&uid);
                    if (uid) { info.updateId = bstrToString(uid); }
                    identity->Release();
                }

                installed.push_back(std::move(info));
                update->Release();
            }
            updates->Release();
        }
        result->Release();
    }

    searcher->Release();
    session->Release();
    CoUninitialize();

    {
        std::lock_guard<std::mutex> lock(mutex_);
        installedUpdates_ = installed;
    }

    LOG_INFO("PatchManager: Found {} installed updates", installed.size());
    return installed;
}

bool PatchManager::installUpdates(const std::vector<std::string>& updateIds) {
    LOG_INFO("PatchManager: Installing {} updates...", updateIds.size());

    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    bool comInitialized = SUCCEEDED(hr) || hr == RPC_E_CHANGED_MODE;
    if (!comInitialized) {
        LOG_ERROR("PatchManager: COM init failed for install");
        return false;
    }

    IUpdateSession* session = nullptr;
    hr = CoCreateInstance(CLSID_UpdateSession_Impl, nullptr, CLSCTX_INPROC_SERVER,
                          IID_IUpdateSession_Impl, reinterpret_cast<void**>(&session));
    if (FAILED(hr) || !session) { CoUninitialize(); return false; }

    IUpdateSearcher* searcher = nullptr;
    session->CreateUpdateSearcher(&searcher);
    if (!searcher) { session->Release(); CoUninitialize(); return false; }

    // Find pending updates
    ISearchResult* searchResult = nullptr;
    BSTR criteria = SysAllocString(L"IsInstalled=0");
    hr = searcher->Search(criteria, &searchResult);
    SysFreeString(criteria);

    if (FAILED(hr) || !searchResult) {
        searcher->Release(); session->Release(); CoUninitialize();
        return false;
    }

    IUpdateCollection* allUpdates = nullptr;
    searchResult->get_Updates(&allUpdates);

    // Create collection of updates to install
    IUpdateCollection* toInstall = nullptr;
    CoCreateInstance(CLSID_UpdateColl_Impl, nullptr, CLSCTX_INPROC_SERVER,
                     IID_IUpdateCollection_Impl, reinterpret_cast<void**>(&toInstall));

    if (!toInstall || !allUpdates) {
        if (allUpdates) allUpdates->Release();
        searchResult->Release(); searcher->Release();
        session->Release(); CoUninitialize();
        return false;
    }

    LONG count = 0;
    allUpdates->get_Count(&count);

    for (LONG i = 0; i < count; ++i) {
        IUpdate* update = nullptr;
        allUpdates->get_Item(i, &update);
        if (!update) continue;

        IUpdateIdentity* identity = nullptr;
        update->get_Identity(&identity);
        if (identity) {
            BSTR uid = nullptr;
            identity->get_UpdateID(&uid);
            if (uid) {
                std::string id = bstrToString(uid);
                for (const auto& targetId : updateIds) {
                    if (id == targetId) {
                        LONG idx;
                        toInstall->Add(update, &idx);
                        break;
                    }
                }
            }
            identity->Release();
        }
        update->Release();
    }

    LONG installCount = 0;
    toInstall->get_Count(&installCount);

    if (installCount == 0) {
        LOG_WARN("PatchManager: No matching updates found to install");
        toInstall->Release(); allUpdates->Release();
        searchResult->Release(); searcher->Release();
        session->Release(); CoUninitialize();
        return false;
    }

    // Download updates
    IUpdateDownloader* downloader = nullptr;
    session->CreateUpdateDownloader(&downloader);
    if (downloader) {
        downloader->put_Updates(toInstall);
        IDownloadResult* dlResult = nullptr;
        hr = downloader->Download(&dlResult);

        if (FAILED(hr)) {
            LOG_ERROR("PatchManager: Download failed: 0x{:08X}", (unsigned)hr);
            if (dlResult) dlResult->Release();
            downloader->Release(); toInstall->Release();
            allUpdates->Release(); searchResult->Release();
            searcher->Release(); session->Release(); CoUninitialize();
            return false;
        }

        OperationResultCode dlResultCode;
        if (dlResult) {
            dlResult->get_ResultCode(&dlResultCode);
            dlResult->Release();
            if (dlResultCode != orcSucceeded) {
                LOG_ERROR("PatchManager: Download result not success: {}", (int)dlResultCode);
                downloader->Release(); toInstall->Release();
                allUpdates->Release(); searchResult->Release();
                searcher->Release(); session->Release(); CoUninitialize();
                return false;
            }
        }
        downloader->Release();
    }

    // Install updates
    IUpdateInstaller* installer = nullptr;
    session->CreateUpdateInstaller(&installer);
    bool success = false;

    if (installer) {
        installer->put_Updates(toInstall);
        IInstallationResult* instResult = nullptr;
        hr = installer->Install(&instResult);

        if (SUCCEEDED(hr) && instResult) {
            OperationResultCode resultCode;
            instResult->get_ResultCode(&resultCode);
            success = (resultCode == orcSucceeded || resultCode == orcSucceededWithErrors);

            VARIANT_BOOL rebootRequired;
            instResult->get_RebootRequired(&rebootRequired);

            LOG_INFO("PatchManager: Install result={}, rebootRequired={}",
                     (int)resultCode, rebootRequired == VARIANT_TRUE);

            if (eventCallback_) {
                nlohmann::json event;
                event["type"] = "patch_install_complete";
                event["timestamp"] = getCurrentTimestamp();
                event["installedCount"] = installCount;
                event["success"] = success;
                event["rebootRequired"] = (rebootRequired == VARIANT_TRUE);
                eventCallback_(event);
            }

            instResult->Release();
        }
        installer->Release();
    }

    toInstall->Release();
    allUpdates->Release();
    searchResult->Release();
    searcher->Release();
    session->Release();
    CoUninitialize();

    return success;
}

bool PatchManager::applyPolicy(const nlohmann::json& policy) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (policy.contains("auto_scan")) {
        config_.autoScan = policy["auto_scan"].get<bool>();
    }
    if (policy.contains("scan_interval_hours")) {
        config_.scanIntervalHours = policy["scan_interval_hours"].get<int>();
    }
    if (policy.contains("auto_install")) {
        config_.autoInstall = policy["auto_install"].get<bool>();
    }
    if (policy.contains("exclude_kbs")) {
        config_.excludeKBs = policy["exclude_kbs"].get<std::vector<std::string>>();
    }

    // Handle immediate scan request
    if (policy.contains("trigger_scan") && policy["trigger_scan"].get<bool>()) {
        std::thread([this]() { scanForUpdates(); }).detach();
    }

    // Handle install request
    if (policy.contains("install_update_ids")) {
        auto ids = policy["install_update_ids"].get<std::vector<std::string>>();
        std::thread([this, ids]() { installUpdates(ids); }).detach();
    }

    LOG_INFO("PatchManager: Policy applied");
    return true;
}

nlohmann::json PatchManager::getStatus() const {
    std::lock_guard<std::mutex> lock(mutex_);
    nlohmann::json status;
    status["enabled"] = config_.enabled;
    status["autoScan"] = config_.autoScan;
    status["autoInstall"] = config_.autoInstall;
    status["scanIntervalHours"] = config_.scanIntervalHours;
    status["lastScanTime"] = lastScanTime_;
    status["pendingUpdateCount"] = lastScanPendingCount_;
    status["running"] = running_.load();
    return status;
}

void PatchManager::setEventCallback(PatchEventCallback callback) {
    eventCallback_ = std::move(callback);
}

void PatchManager::scanLoop() {
    LOG_DEBUG("PatchManager: Scan loop thread started");

    // Initial scan at startup
    scanForUpdates();

    while (running_.load()) {
        auto waitEnd = std::chrono::steady_clock::now() +
                       std::chrono::hours(config_.scanIntervalHours);

        while (std::chrono::steady_clock::now() < waitEnd && running_.load()) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }

        if (!running_.load()) break;

        auto results = scanForUpdates();

        // Auto-install if configured
        if (config_.autoInstall && !results.empty()) {
            std::vector<std::string> ids;
            for (const auto& p : results) {
                ids.push_back(p.updateId);
            }
            LOG_INFO("PatchManager: Auto-installing {} updates", ids.size());
            installUpdates(ids);
        }
    }

    LOG_DEBUG("PatchManager: Scan loop thread stopped");
}

} // namespace ResolutePulse

