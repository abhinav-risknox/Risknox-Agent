#include "manager/config/ManagerConfig.h"
#include "utils/Logger.h"

#include <iostream>
#include <fstream>
#include <cstdlib>
#include <filesystem>

namespace ResolutePulse
{

    bool ManagerConfig::load(int argc, char *argv[])
    {
        // Step 1: Pre-pass CLI args to check for explicit --config or -c flag
        std::string configFile;
        for (int i = 1; i < argc; i++)
        {
            std::string arg = argv[i];
            if ((arg == "--config" || arg == "-c") && i + 1 < argc)
            {
                configFile = argv[++i];
                break;
            }
        }

        // Step 2: Check RPLS_CONFIG env var if no CLI flag provided
        if (configFile.empty())
        {
            const char *envCfg = std::getenv("RPLS_CONFIG");
            if (envCfg)
            {
                configFile = envCfg;
            }
        }

        // Step 3: Default to manager_config.json if it exists and no explicit path set
        if (configFile.empty() && std::filesystem::exists("manager_config.json"))
        {
            configFile = "manager_config.json";
        }

        // Step 4: Load from JSON file if found/specified
        if (!configFile.empty())
        {
            config_.configFilePath = configFile;
            if (!loadFromFile(configFile))
            {
                LOG_WARN("Failed to parse config file: {}. Continuing with defaults/env/cli.", configFile);
            }
            else
            {
                LOG_INFO("Loaded configuration from: {}", configFile);
            }
        }

        // Step 5: Override with Environment Variables
        loadFromEnv();

        // Step 6: Override with CLI Arguments (Highest Precedence)
        if (!parseCli(argc, argv))
        {
            return false;
        }

        return true;
    }

    bool ManagerConfig::loadFromFile(const std::string &path)
    {
        std::ifstream file(path);
        if (!file.is_open())
        {
            LOG_ERROR("Unable to open config file: {}", path);
            return false;
        }

        try
        {
            nlohmann::json j;
            file >> j;

            if (j.contains("ports") && j["ports"].is_object())
            {
                const auto &p = j["ports"];
                if (p.contains("agent_port") && p["agent_port"].is_number_integer())
                {
                    config_.ports.agentPort = p["agent_port"].get<int>();
                }
                if (p.contains("api_port") && p["api_port"].is_number_integer())
                {
                    config_.ports.apiPort = p["api_port"].get<int>();
                }
                if (p.contains("command_port") && p["command_port"].is_number_integer())
                {
                    config_.ports.commandPort = p["command_port"].get<int>();
                }
            }

            if (j.contains("ca_dir") && j["ca_dir"].is_string())
            {
                config_.caDir = j["ca_dir"].get<std::string>();
            }

            if (j.contains("database") && j["database"].is_object())
            {
                const auto &db = j["database"];
                if (db.contains("connection_string") && db["connection_string"].is_string())
                {
                    config_.dbConnString = db["connection_string"].get<std::string>();
                }
            }

            if (j.contains("logging") && j["logging"].is_object())
            {
                const auto &log = j["logging"];
                if (log.contains("level") && log["level"].is_string())
                {
                    config_.logLevel = log["level"].get<std::string>();
                }
            }
        }
        catch (const std::exception &e)
        {
            LOG_ERROR("JSON parse error in config file {}: {}", path, e.what());
            return false;
        }

        return true;
    }

    void ManagerConfig::loadFromEnv()
    {
        const char *envDb = std::getenv("RPLS_DB_CONN");
        if (envDb)
        {
            config_.dbConnString = envDb;
        }

        const char *envDbPass = std::getenv("DB_PASSWORD");
        if (envDbPass && config_.dbConnString.find("password=") == std::string::npos)
        {
            config_.dbConnString += " password=";
            config_.dbConnString += envDbPass;
        }

        const char *envPort = std::getenv("RPLS_PORT");
        if (envPort)
        {
            try
            {
                config_.ports.agentPort = std::stoi(envPort);
            }
            catch (...)
            {
            }
        }

        const char *envApiPort = std::getenv("RPLS_API_PORT");
        if (envApiPort)
        {
            try
            {
                config_.ports.apiPort = std::stoi(envApiPort);
            }
            catch (...)
            {
            }
        }

        const char *envCmdPort = std::getenv("RPLS_COMMAND_PORT");
        if (envCmdPort)
        {
            try
            {
                config_.ports.commandPort = std::stoi(envCmdPort);
            }
            catch (...)
            {
            }
        }
    }

    bool ManagerConfig::parseCli(int argc, char *argv[])
    {
        const char *envDbPass = std::getenv("DB_PASSWORD");

        for (int i = 1; i < argc; i++)
        {
            std::string arg = argv[i];
            if ((arg == "--db" || arg == "-d") && i + 1 < argc)
            {
                config_.dbConnString = argv[++i];
                if (config_.dbConnString.find("password=") == std::string::npos && envDbPass)
                {
                    config_.dbConnString += " password=";
                    config_.dbConnString += envDbPass;
                }
            }
            else if (arg == "--ca-dir" && i + 1 < argc)
            {
                config_.caDir = argv[++i];
            }
            else if ((arg == "--port" || arg == "-p") && i + 1 < argc)
            {
                config_.ports.agentPort = std::stoi(argv[++i]);
            }
            else if (arg == "--api-port" && i + 1 < argc)
            {
                config_.ports.apiPort = std::stoi(argv[++i]);
            }
            else if (arg == "--command-port" && i + 1 < argc)
            {
                config_.ports.commandPort = std::stoi(argv[++i]);
            }
            else if ((arg == "--config" || arg == "-c") && i + 1 < argc)
            {
                // Already processed in load(), advance index
                i++;
            }
            else if (arg == "--help" || arg == "-h")
            {
                printHelp(argv[0]);
                return false; // Stop execution after printing help
            }
        }

        return true;
    }

    void ManagerConfig::printHelp(const char *progName)
    {
        std::cout << "ResolutePulse Manager Server" << std::endl;
        std::cout << "Usage: " << progName << " [options]" << std::endl;
        std::cout << "Options:" << std::endl;
        std::cout << "  --config, -c FILE     Path to JSON configuration file" << std::endl;
        std::cout << "  --port, -p PORT       mTLS Agent listen port (default: 1514)" << std::endl;
        std::cout << "  --api-port PORT       REST API listen port (default: 8080)" << std::endl;
        std::cout << "  --command-port PORT   Command ingest loopback port (default: 1515)" << std::endl;
        std::cout << "  --db, -d CONN         PostgreSQL connection string" << std::endl;
        std::cout << "  --ca-dir DIR          CA directory (default: ca)" << std::endl;
        std::cout << "  --help, -h            Show this help text" << std::endl;
    }

} // namespace ResolutePulse
