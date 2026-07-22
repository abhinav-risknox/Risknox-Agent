#pragma once

#include <string>
#include <nlohmann/json.hpp>

namespace ResolutePulse
{

    struct PortsConfig
    {
        int agentPort = 1514;   // mTLS agent communication port
        int apiPort = 8080;     // REST API HTTP port
        int commandPort = 1515; // Command ingest loopback port
    };

    struct ManagerConfigData
    {
        PortsConfig ports;
        std::string dbConnString = "host=127.0.0.1 port=5432 dbname=risknox user=postgres password=abhi1243";
        std::string caDir = "ca";
        std::string logLevel = "info";
        std::string configFilePath;
    };

    class ManagerConfig
    {
    public:
        ManagerConfig() = default;

        // Load configuration following precedence:
        // Built-in Defaults -> JSON File -> Environment Variables -> CLI Arguments
        bool load(int argc, char *argv[]);

        const ManagerConfigData &get() const { return config_; }

        static void printHelp(const char *progName);

    private:
        bool loadFromFile(const std::string &path);
        void loadFromEnv();
        bool parseCli(int argc, char *argv[]);

        ManagerConfigData config_;
    };

} // namespace ResolutePulse
