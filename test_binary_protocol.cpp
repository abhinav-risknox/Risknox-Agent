// Test program for BinaryProtocol
#include "src/network/BinaryProtocol.h"
#include <iostream>
#include <iomanip>

using namespace ResolutePulse;

void printHex(const std::vector<uint8_t>& data, size_t limit = 100) {
    std::cout << "Hex dump (" << data.size() << " bytes):\n";
    for (size_t i = 0; i < std::min(data.size(), limit); i++) {
        std::cout << std::hex << std::setw(2) << std::setfill('0') 
                  << static_cast<int>(data[i]) << " ";
        if ((i + 1) % 16 == 0) std::cout << "\n";
    }
    std::cout << std::dec << "\n\n";
}

int main() {
    std::cout << "=== Binary Protocol Test ===\n\n";
    
    // Create test message
    BinaryMessage msg;
    msg.agentId = "test-agent-001";
    msg.xmlData = "<Event xmlns='http://schemas.microsoft.com/win/2004/08/events/event'>"
                  "<System><EventID>4624</EventID><Computer>TEST-PC</Computer></System>"
                  "</Event>";
    
    std::cout << "Original Message:\n";
    std::cout << "  Agent ID: " << msg.agentId << "\n";
    std::cout << "  XML Length: " << msg.xmlData.length() << " bytes\n";
    std::cout << "  XML Preview: " << msg.xmlData.substr(0, 80) << "...\n\n";
    
    // Serialize
    std::cout << "Serializing...\n";
    auto serialized = msg.serialize();
    
    std::cout << "Serialized Size: " << serialized.size() << " bytes\n";
    std::cout << "  - Length prefix: 4 bytes\n";
    std::cout << "  - Agent ID: 32 bytes\n";
    std::cout << "  - XML data: " << msg.xmlData.length() << " bytes\n";
    std::cout << "  - Total expected: " << (4 + 32 + msg.xmlData.length()) << " bytes\n\n";
    
    printHex(serialized, 80);
    
    // Deserialize
    std::cout << "Deserializing...\n";
    try {
        BinaryMessage decoded = BinaryMessage::deserialize(serialized.data(), serialized.size());
        
        std::cout << "Decoded Message:\n";
        std::cout << "  Agent ID: " << decoded.agentId << "\n";
        std::cout << "  XML Length: " << decoded.xmlData.length() << " bytes\n";
        std::cout << "  XML Match: " << (decoded.xmlData == msg.xmlData ? "YES ✓" : "NO ✗") << "\n\n";
        
        // Verify
        if (decoded.agentId == msg.agentId && decoded.xmlData == msg.xmlData) {
            std::cout << "✓ SUCCESS: Binary protocol working correctly!\n";
            return 0;
        } else {
            std::cout << "✗ FAILURE: Data mismatch after deserialization\n";
            return 1;
        }
        
    } catch (const std::exception& e) {
        std::cout << "✗ ERROR: " << e.what() << "\n";
        return 1;
    }
}
