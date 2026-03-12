#include "manager/db/PostgresClient.h"
#include "utils/Logger.h"
#include <iostream>
#include <cstdlib>

using namespace ResolutePulse;

int main() {
    Logger::initialize("info");
    PostgresClient db;
    
    const char* connStr = std::getenv("RPLS_DB_CONN");
    if (!connStr) {
        connStr = "host=127.0.0.1 dbname=risknox user=postgres password=abhi1243";
    }
    
    if (!db.connect(connStr)) {
        std::cerr << "Failed to connect to DB" << std::endl;
        return 1;
    }
    
    std::cout << "--- Recent Licenses ---" << std::endl;
    // We don't have a listLicenses method, but we can try to getLicense for the known test ID
    auto lic = db.getLicense("test-tls-agent-902360");
    if (lic.has_value()) {
        std::cout << "License Found!" << std::endl;
        std::cout << "Agent ID: " << lic->agentId << std::endl;
        std::cout << "Type:     " << lic->licenseType << std::endl;
        std::cout << "From:     " << lic->validFrom << std::endl;
        std::cout << "Until:    " << lic->validUntil << std::endl;
    } else {
        std::cout << "License NOT found for test agent ID." << std::endl;
    }
    
    return 0;
}
