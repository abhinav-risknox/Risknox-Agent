#include "ConfigManager.h"
#include "utils/Logger.h"
#include <fstream>

#ifdef _WIN32
#include <windows.h>
#include <Wbemidl.h>
#include <functional>
#endif

namespace ResolutePulse {

ConfigManager& ConfigManager::instance() {
    static ConfigManager instance;
    return instance;
}

bool ConfigManager::load(const std::string& configPath) {
    try {
        std::ifstream file(configPath);
        if (!file.is_open()) {
            LOG_ERROR("Failed to open config file: {}", configPath);
            return false;
        }
        
        nlohmann::json config = nlohmann::json::parse(file);
        
        // TCP configuration (optional - has defaults)
        fluentBitHost_ = config.value("fluent_bit_host", "localhost");
        fluentBitPort_ = config.value("fluent_bit_port", 5170);
        
        // TLS configuration for Fluent Bit connection
        fluentBitTlsEnabled_ = config.value("fluent_bit_tls", false);
        fluentBitCaCertPath_ = config.value("fluent_bit_ca_cert", "");
        
        // Validate port
        if (fluentBitPort_ < 1 || fluentBitPort_ > 65535) {
            LOG_ERROR("Invalid fluent_bit_port: {}", fluentBitPort_);
            return false;
        }
        
        // Agent ID (required)
        if (!config.contains("agent_id")) {
            LOG_ERROR("Config missing required field: agent_id");
            return false;
        }
        agentId_ = config["agent_id"].get<std::string>();

        if (agentId_ == "AUTO") {
            const char* programData = std::getenv("ProgramData");
            std::filesystem::path idFile = std::filesystem::path(programData ? programData : "C:\\ProgramData") / "Risknox Pulse" / "agent_id.txt";
            
            if (std::filesystem::exists(idFile)) {
                std::ifstream f(idFile);
                std::getline(f, agentId_);
            } else {
                char buffer[MAX_COMPUTERNAME_LENGTH + 1];
                DWORD size = sizeof(buffer);
                std::string compName = "Unknown";
                if (GetComputerNameA(buffer, &size)) {
                    compName = buffer;
                }
                
                // Generate a deterministic suffix from hardware identifiers
                // so the same machine always produces the same agent ID,
                // even after uninstall/reinstall.
                std::string machineFingerprint;
                
                // Primary: SMBIOS UUID (hardware-rooted, survives OS reinstall)
                machineFingerprint = getSmbiosUuid();
                
                // Fallback: Windows MachineGuid (survives agent uninstall)
                if (machineFingerprint.empty()) {
                    LOG_WARN("SMBIOS UUID unavailable, falling back to MachineGuid");
                    machineFingerprint = getMachineGuid();
                }
                
                if (!machineFingerprint.empty()) {
                    // Hash the fingerprint to a short numeric suffix
                    std::hash<std::string> hasher;
                    size_t hashVal = hasher(machineFingerprint);
                    // Take lower 5 digits for a compact, readable suffix
                    int suffix = static_cast<int>(hashVal % 100000);
                    agentId_ = compName + "-" + std::to_string(suffix);
                    LOG_INFO("Generated deterministic agent ID from hardware fingerprint");
                } else {
                    // Last resort: random suffix (should rarely happen)
                    LOG_WARN("No hardware fingerprint available, using random suffix");
                    srand(static_cast<unsigned int>(time(nullptr)));
                    int randomSuffix = rand() % 100000;
                    agentId_ = compName + "-" + std::to_string(randomSuffix);
                }
                
                // Cache the generated ID for faster startup next time
                std::filesystem::create_directories(idFile.parent_path());
                std::ofstream f(idFile);
                f << agentId_;
            }
        }
        
        // Manager config (optional)
        if (config.contains("manager")) {
            auto& mgr = config["manager"];
            managerConfig_.enabled = mgr.value("enabled", false);
            managerConfig_.host = mgr.value("host", "localhost");
            managerConfig_.port = mgr.value("port", 1514);
            managerConfig_.certs_dir = mgr.value("certs_dir", "certs");
            managerConfig_.registration_retry_interval = mgr.value("registration_retry_interval", 30);
            managerConfig_.heartbeat_interval = mgr.value("heartbeat_interval", 60);
            managerConfig_.license_check_interval = mgr.value("license_check_interval", 3600);
        }
        
        // Buffer config (optional)
        if (config.contains("buffer")) {
            auto& buffer = config["buffer"];
            bufferConfig_.max_events = buffer.value("max_events", 50000);
            bufferConfig_.flush_interval_sec = buffer.value("flush_interval_sec", 10);
            bufferConfig_.db_path = buffer.value("db_path", "events_buffer.db");
        }
        
        // Event collection config (optional)
        if (config.contains("event_collection")) {
            auto& ec = config["event_collection"];
            eventCollectionConfig_.collect_historical_on_startup = 
                ec.value("collect_historical_on_startup", false);
            eventCollectionConfig_.historical_hours_back = 
                ec.value("historical_hours_back", 24);
            eventCollectionConfig_.max_historical_events_per_channel = 
                ec.value("max_historical_events_per_channel", 10000);
        }
        
        // Critical channels (default to Security and System)
        if (config.contains("critical_channels")) {
            criticalChannels_ = config["critical_channels"].get<std::vector<std::string>>();
        } else {
            criticalChannels_ = {"Security", "System"};
        }
        
        // Event channels (required)
        if (!config.contains("event_channels")) {
            LOG_ERROR("Config missing required field: event_channels");
            return false;
        }
        eventChannels_ = config["event_channels"].get<std::vector<std::string>>();
        
        // Event filters (optional - if not specified, collect all events)
        if (config.contains("event_filters")) {
            auto& filters = config["event_filters"];
            for (auto& [channel, ids] : filters.items()) {
                std::set<int> idSet;
                for (auto& id : ids) {
                    idSet.insert(id.get<int>());
                }
                eventFilters_[channel] = idSet;
            }
        }
        
        // Log level (optional)
        logLevel_ = config.value("log_level", "info");
        
        // FIM config (optional)
        if (config.contains("fim")) {
            auto& fim = config["fim"];
            fimConfig_.enabled = fim.value("enabled", false);
            if (fim.contains("directories")) {
                fimConfig_.directories = fim["directories"].get<std::vector<std::string>>();
            }
            if (fim.contains("exclude_patterns")) {
                fimConfig_.exclude_patterns = fim["exclude_patterns"].get<std::vector<std::string>>();
            }
            fimConfig_.max_file_size_mb = fim.value("max_file_size_mb", 100);
            fimConfig_.baseline_interval_hours = fim.value("baseline_interval_hours", 24);
            fimConfig_.db_path = fim.value("db_path", "fim_baseline.db");
        }
        
        // System info config (optional)
        if (config.contains("system_info")) {
            auto& sysInfo = config["system_info"];
            sysInfoConfig_.enabled = sysInfo.value("enabled", true);
            sysInfoConfig_.collect_on_startup = sysInfo.value("collect_on_startup", true);
            sysInfoConfig_.collection_interval_hours = sysInfo.value("collection_interval_hours", 24);
            
            if (sysInfo.contains("installed_apps")) {
                auto& apps = sysInfo["installed_apps"];
                sysInfoConfig_.apps.include_install_date = apps.value("include_install_date", true);
                sysInfoConfig_.apps.include_install_location = apps.value("include_install_location", true);
                sysInfoConfig_.apps.include_size = apps.value("include_size", true);
            }
            
            if (sysInfo.contains("open_ports")) {
                auto& ports = sysInfo["open_ports"];
                sysInfoConfig_.ports.include_listening = ports.value("include_listening", true);
                sysInfoConfig_.ports.include_established = ports.value("include_established", true);
                sysInfoConfig_.ports.include_process_info = ports.value("include_process_info", true);
            }
        }
        
        // Patch management config (optional)
        if (config.contains("patch_management")) {
            auto& pm = config["patch_management"];
            patchConfig_.enabled = pm.value("enabled", false);
            patchConfig_.auto_scan = pm.value("auto_scan", true);
            patchConfig_.scan_interval_hours = pm.value("scan_interval_hours", 24);
            patchConfig_.auto_install = pm.value("auto_install", false);
            if (pm.contains("exclude_kbs")) {
                patchConfig_.exclude_kbs = pm["exclude_kbs"].get<std::vector<std::string>>();
            }
        }
        
        // Web blocking config (optional)
        if (config.contains("web_blocking")) {
            auto& wb = config["web_blocking"];
            webBlockConfig_.enabled = wb.value("enabled", false);
        }
        
        // Software/application blocking config (optional)
        if (config.contains("software_blocking")) {
            auto& ab = config["software_blocking"];
            appBlockConfig_.enabled = ab.value("enabled", false);
            appBlockConfig_.monitor_interval_ms = ab.value("monitor_interval_ms", 300);
        }

        // Antivirus config (optional)
        if (config.contains("antivirus")) {
            auto& av = config["antivirus"];
            avConfig_.enabled                 = av.value("enabled", false);
            avConfig_.auto_scan               = av.value("auto_scan", false);
            avConfig_.scan_interval_hours     = av.value("scan_interval_hours", 24);
            avConfig_.auto_update_definitions = av.value("auto_update_definitions", true);
            avConfig_.update_interval_hours   = av.value("update_interval_hours", 12);
            if (av.contains("scan_paths")) {
                avConfig_.scan_paths = av["scan_paths"].get<std::vector<std::string>>();
            }
            if (avConfig_.scan_paths.empty()) {
                avConfig_.scan_paths = { "C:\\Users" };  // sensible default
            }
        }

        // USB scan config (optional)
        if (config.contains("usb_scan")) {
            auto& usb = config["usb_scan"];
            usbScanConfig_.enabled            = usb.value("enabled", false);
            usbScanConfig_.scan_delay_seconds = usb.value("scan_delay_seconds", 2);
        }

        // Download/new-file scan config (optional)
        if (config.contains("download_scan")) {
            auto& ds = config["download_scan"];
            downloadScanConfig_.enabled          = ds.value("enabled", false);
            downloadScanConfig_.motw_only         = ds.value("motw_only", true);
            downloadScanConfig_.debounce_seconds  = ds.value("debounce_seconds", 5);
            if (ds.contains("watch_paths")) {
                downloadScanConfig_.watch_paths = ds["watch_paths"].get<std::vector<std::string>>();
            }
            if (ds.contains("scan_extensions")) {
                downloadScanConfig_.scan_extensions = ds["scan_extensions"].get<std::vector<std::string>>();
            }
        }
        
        loaded_ = true;
        LOG_INFO("Configuration loaded successfully from: {}", configPath);
        LOG_INFO("Agent ID: {}", agentId_);
        LOG_INFO("Fluent Bit: {}:{}", fluentBitHost_, fluentBitPort_);
        LOG_INFO("Event channels configured: {}", eventChannels_.size());
        LOG_INFO("Critical channels: {}", criticalChannels_.size());
        if (fimConfig_.enabled) {
            LOG_INFO("FIM enabled: {} directories", fimConfig_.directories.size());
        }
        if (sysInfoConfig_.enabled) {
            LOG_INFO("System Info enabled: collect on startup={}, interval={}h", 
                     sysInfoConfig_.collect_on_startup, 
                     sysInfoConfig_.collection_interval_hours);
        }
        if (patchConfig_.enabled) {
            LOG_INFO("Patch Management enabled: autoScan={}, interval={}h",
                     patchConfig_.auto_scan, patchConfig_.scan_interval_hours);
        }
        if (webBlockConfig_.enabled) {
            LOG_INFO("Web Blocking enabled");
        }
        if (appBlockConfig_.enabled) {
            LOG_INFO("Software Blocking enabled: monitorInterval={}ms",
                     appBlockConfig_.monitor_interval_ms);
        }
        if (avConfig_.enabled) {
            LOG_INFO("Antivirus enabled: autoScan={}, interval={}h, paths={}",
                     avConfig_.auto_scan, avConfig_.scan_interval_hours,
                     avConfig_.scan_paths.size());
        }
        
        return true;
        
    } catch (const nlohmann::json::exception& e) {
        LOG_ERROR("JSON parsing error: {}", e.what());
        return false;
    } catch (const std::exception& e) {
        LOG_ERROR("Error loading config: {}", e.what());
        return false;
    }
}

bool ConfigManager::shouldCollectEvent(const std::string& channel, int eventId) const {
    // If no filter is defined for this channel, collect all events
    auto it = eventFilters_.find(channel);
    if (it == eventFilters_.end()) {
        return true;
    }
    
    // Check if eventId is in the allowed set
    return it->second.count(eventId) > 0;
}

#ifdef _WIN32
std::string ConfigManager::getSmbiosUuid() {
    // Query WMI for the SMBIOS UUID (Win32_ComputerSystemProduct.UUID)
    // This is burned into the motherboard firmware and survives OS reinstalls.
    // Uses raw COM/BSTR calls for MinGW compatibility (no _bstr_t / comdef.h).
    std::string uuid;

    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    bool needUninit = SUCCEEDED(hr);
    // S_FALSE means COM was already initialized — that's fine, just don't uninit.
    if (FAILED(hr) && hr != RPC_E_CHANGED_MODE) {
        LOG_WARN("getSmbiosUuid: CoInitializeEx failed: 0x{:08X}", static_cast<unsigned>(hr));
        return "";
    }

    hr = CoInitializeSecurity(nullptr, -1, nullptr, nullptr,
                              RPC_C_AUTHN_LEVEL_DEFAULT,
                              RPC_C_IMP_LEVEL_IMPERSONATE,
                              nullptr, EOAC_NONE, nullptr);
    // RPC_E_TOO_LATE is harmless — security was already set by a prior call.
    if (FAILED(hr) && hr != RPC_E_TOO_LATE) {
        LOG_WARN("getSmbiosUuid: CoInitializeSecurity failed: 0x{:08X}", static_cast<unsigned>(hr));
        if (needUninit) CoUninitialize();
        return "";
    }

    IWbemLocator* pLoc = nullptr;
    hr = CoCreateInstance(CLSID_WbemLocator, nullptr, CLSCTX_INPROC_SERVER,
                          IID_IWbemLocator, reinterpret_cast<void**>(&pLoc));
    if (FAILED(hr)) {
        LOG_WARN("getSmbiosUuid: CoCreateInstance failed: 0x{:08X}", static_cast<unsigned>(hr));
        if (needUninit) CoUninitialize();
        return "";
    }

    IWbemServices* pSvc = nullptr;
    BSTR bstrNamespace = SysAllocString(L"ROOT\\CIMV2");
    hr = pLoc->ConnectServer(bstrNamespace, nullptr, nullptr,
                              nullptr, 0, nullptr, nullptr, &pSvc);
    SysFreeString(bstrNamespace);
    if (FAILED(hr)) {
        LOG_WARN("getSmbiosUuid: ConnectServer failed: 0x{:08X}", static_cast<unsigned>(hr));
        pLoc->Release();
        if (needUninit) CoUninitialize();
        return "";
    }

    hr = CoSetProxyBlanket(pSvc, RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE, nullptr,
                            RPC_C_AUTHN_LEVEL_CALL, RPC_C_IMP_LEVEL_IMPERSONATE,
                            nullptr, EOAC_NONE);

    IEnumWbemClassObject* pEnum = nullptr;
    BSTR bstrWql   = SysAllocString(L"WQL");
    BSTR bstrQuery = SysAllocString(L"SELECT UUID FROM Win32_ComputerSystemProduct");
    hr = pSvc->ExecQuery(bstrWql, bstrQuery,
                          WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY,
                          nullptr, &pEnum);
    SysFreeString(bstrWql);
    SysFreeString(bstrQuery);

    if (SUCCEEDED(hr) && pEnum) {
        IWbemClassObject* pObj = nullptr;
        ULONG uReturn = 0;
        if (pEnum->Next(WBEM_INFINITE, 1, &pObj, &uReturn) == S_OK && uReturn == 1) {
            VARIANT vtProp;
            VariantInit(&vtProp);
            if (SUCCEEDED(pObj->Get(L"UUID", 0, &vtProp, nullptr, nullptr))) {
                if (vtProp.vt == VT_BSTR && vtProp.bstrVal) {
                    // Convert wide BSTR to narrow std::string
                    int wlen = SysStringLen(vtProp.bstrVal);
                    int needed = WideCharToMultiByte(CP_UTF8, 0, vtProp.bstrVal, wlen, nullptr, 0, nullptr, nullptr);
                    if (needed > 0) {
                        uuid.resize(needed);
                        WideCharToMultiByte(CP_UTF8, 0, vtProp.bstrVal, wlen, &uuid[0], needed, nullptr, nullptr);
                    }
                }
                VariantClear(&vtProp);
            }
            pObj->Release();
        }
        pEnum->Release();
    }

    pSvc->Release();
    pLoc->Release();
    if (needUninit) CoUninitialize();

    // Reject known-bad UUIDs (all zeros, all Fs, or "Not Settable")
    if (!uuid.empty()) {
        std::string upper = uuid;
        for (auto& c : upper) c = static_cast<char>(toupper(c));
        if (upper == "FFFFFFFF-FFFF-FFFF-FFFF-FFFFFFFFFFFF" ||
            upper == "00000000-0000-0000-0000-000000000000" ||
            upper.find("NOT") != std::string::npos) {
            LOG_WARN("SMBIOS UUID is invalid/default: {}", uuid);
            return "";
        }
    }

    return uuid;
}

std::string ConfigManager::getMachineGuid() {
    // Read the Windows MachineGuid from the registry.
    // This is generated during OS installation and persists across
    // software uninstalls — only changes on a full OS reinstall.
    HKEY hKey;
    LONG result = RegOpenKeyExA(HKEY_LOCAL_MACHINE,
                                 "SOFTWARE\\Microsoft\\Cryptography",
                                 0, KEY_READ | KEY_WOW64_64KEY, &hKey);
    if (result != ERROR_SUCCESS) {
        LOG_WARN("getMachineGuid: RegOpenKeyEx failed: {}", result);
        return "";
    }

    char buffer[256] = {};
    DWORD bufSize = sizeof(buffer);
    DWORD type = 0;
    result = RegQueryValueExA(hKey, "MachineGuid", nullptr, &type,
                               reinterpret_cast<LPBYTE>(buffer), &bufSize);
    RegCloseKey(hKey);

    if (result != ERROR_SUCCESS || type != REG_SZ) {
        LOG_WARN("getMachineGuid: RegQueryValueEx failed: {}", result);
        return "";
    }

    return std::string(buffer);
}
#endif

} // namespace ResolutePulse
