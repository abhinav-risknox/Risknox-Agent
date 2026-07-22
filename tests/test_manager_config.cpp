#include "manager/config/ManagerConfig.h"
#include "utils/Logger.h"

#include <iostream>
#include <cassert>
#include <fstream>
#include <filesystem>
#include <cstdlib>

using namespace ResolutePulse;

void testDefaultConfig()
{
    std::cout << "\n========== TEST 1: Default Configuration ==========\n";
    ManagerConfig config;

    char *argv[] = {const_cast<char *>("ResolutePulseManager")};
    int argc = 1;

    bool ok = config.load(argc, argv);
    assert(ok && "load() should succeed with default arguments");

    const auto &c = config.get();
    assert(c.ports.agentPort == 1514 && "Default agentPort must be 1514");
    assert(c.ports.apiPort == 8080 && "Default apiPort must be 8080");
    assert(c.ports.commandPort == 1515 && "Default commandPort must be 1515");
    assert(c.caDir == "ca" && "Default caDir must be 'ca'");

    std::cout << "[PASS] Default configuration validated (ports: 1514, 8080, 1515)\n";
}

void testJsonConfigFile()
{
    std::cout << "\n========== TEST 2: JSON Config File Loading ==========\n";

    std::string testJsonPath = "test_temp_manager_config.json";
    std::ofstream file(testJsonPath);
    file << R"({
        "ports": {
            "agent_port": 2514,
            "api_port": 9080,
            "command_port": 2515
        },
        "ca_dir": "custom_ca",
        "logging": {
            "level": "debug"
        }
    })";
    file.close();

    ManagerConfig config;
    char *argv[] = {
        const_cast<char *>("ResolutePulseManager"),
        const_cast<char *>("--config"),
        const_cast<char *>(testJsonPath.c_str())};
    int argc = 3;

    bool ok = config.load(argc, argv);
    std::filesystem::remove(testJsonPath);

    assert(ok && "load() should succeed with valid JSON config file");

    const auto &c = config.get();
    assert(c.ports.agentPort == 2514 && "JSON config agentPort must be 2514");
    assert(c.ports.apiPort == 9080 && "JSON config apiPort must be 9080");
    assert(c.ports.commandPort == 2515 && "JSON config commandPort must be 2515");
    assert(c.caDir == "custom_ca" && "JSON config caDir must be 'custom_ca'");
    assert(c.logLevel == "debug" && "JSON config logLevel must be 'debug'");

    std::cout << "[PASS] JSON config file loading validated (ports: 2514, 9080, 2515)\n";
}

void testEnvVarOverrides()
{
    std::cout << "\n========== TEST 3: Environment Variable Overrides ==========\n";

#ifdef _WIN32
    _putenv("RPLS_PORT=3514");
    _putenv("RPLS_API_PORT=10080");
    _putenv("RPLS_COMMAND_PORT=3515");
#else
    setenv("RPLS_PORT", "3514", 1);
    setenv("RPLS_API_PORT", "10080", 1);
    setenv("RPLS_COMMAND_PORT", "3515", 1);
#endif

    ManagerConfig config;
    char *argv[] = {const_cast<char *>("ResolutePulseManager")};
    int argc = 1;

    bool ok = config.load(argc, argv);

#ifdef _WIN32
    _putenv("RPLS_PORT=");
    _putenv("RPLS_API_PORT=");
    _putenv("RPLS_COMMAND_PORT=");
#else
    unsetenv("RPLS_PORT");
    unsetenv("RPLS_API_PORT");
    unsetenv("RPLS_COMMAND_PORT");
#endif

    assert(ok && "load() should succeed with env vars");

    const auto &c = config.get();
    assert(c.ports.agentPort == 3514 && "Env RPLS_PORT must override to 3514");
    assert(c.ports.apiPort == 10080 && "Env RPLS_API_PORT must override to 10080");
    assert(c.ports.commandPort == 3515 && "Env RPLS_COMMAND_PORT must override to 3515");

    std::cout << "[PASS] Environment variable overrides validated (ports: 3514, 10080, 3515)\n";
}

void testCliOverrides()
{
    std::cout << "\n========== TEST 4: Command-Line Overrides (Highest Precedence) ==========\n";

#ifdef _WIN32
    _putenv("RPLS_PORT=3514");
#else
    setenv("RPLS_PORT", "3514", 1);
#endif

    ManagerConfig config;
    char *argv[] = {
        const_cast<char *>("ResolutePulseManager"),
        const_cast<char *>("--port"), const_cast<char *>("4514"),
        const_cast<char *>("--api-port"), const_cast<char *>("11080"),
        const_cast<char *>("--command-port"), const_cast<char *>("4515")};
    int argc = 7;

    bool ok = config.load(argc, argv);

#ifdef _WIN32
    _putenv("RPLS_PORT=");
#else
    unsetenv("RPLS_PORT");
#endif

    assert(ok && "load() should succeed with CLI flags");

    const auto &c = config.get();
    assert(c.ports.agentPort == 4514 && "CLI --port 4514 must override Env 3514");
    assert(c.ports.apiPort == 11080 && "CLI --api-port 11080 must override default");
    assert(c.ports.commandPort == 4515 && "CLI --command-port 4515 must override default");

    std::cout << "[PASS] CLI flag overrides validated (ports: 4514, 11080, 4515)\n";
}

int main()
{
    Logger::initialize("info");
    std::cout << "Starting ManagerConfig test suite...\n";

    testDefaultConfig();
    testJsonConfigFile();
    testEnvVarOverrides();
    testCliOverrides();

    std::cout << "\nAll ManagerConfig tests passed successfully!\n";
    return 0;
}
