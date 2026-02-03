// Simple test to send binary message to Fluent Bit
#include "src/network/BinaryProtocol.h"
#include <iostream>
#include <winsock2.h>
#include <ws2tcpip.h>

#pragma comment(lib, "ws2_32.lib")

using namespace ResolutePulse;

int main() {
    // Initialize Winsock
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);
    
    // Create test message
    BinaryMessage msg;
    msg.agentId = "test-agent-001";
    msg.xmlData = "<Event xmlns='http://schemas.microsoft.com/win/2004/08/events/event'>"
                  "<System>"
                  "<EventID>4624</EventID>"
                  "<Computer>TEST-PC</Computer>"
                  "<Channel>Security</Channel>"
                  "</System>"
                  "</Event>";
    
    std::cout << "Sending binary message to Fluent Bit...\n";
    std::cout << "Agent ID: " << msg.agentId << "\n";
    std::cout << "XML size: " << msg.xmlData.size() << " bytes\n";
    
    // Serialize
    auto data = msg.serialize();
    std::cout << "Serialized size: " << data.size() << " bytes\n\n";
    
    // Connect to Fluent Bit
    SOCKET sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == INVALID_SOCKET) {
        std::cerr << "Failed to create socket\n";
        return 1;
    }
    
    sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(5170);
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
    
    if (connect(sock, (sockaddr*)&addr, sizeof(addr)) != 0) {
        std::cerr << "Failed to connect to Fluent Bit\n";
        closesocket(sock);
        return 1;
    }
    
    std::cout << "Connected to Fluent Bit on port 5170\n";
    
    // Send binary message
    int sent = send(sock, (char*)data.data(), data.size(), 0);
    if (sent > 0) {
        std::cout << "Sent " << sent << " bytes successfully!\n";
    } else {
        std::cerr << "Send failed\n";
    }
    
    closesocket(sock);
    WSACleanup();
    
    std::cout << "\nTest message sent. Check Fluent Bit logs and OpenSearch.\n";
    return 0;
}
