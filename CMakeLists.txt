cmake_minimum_required(VERSION 3.20)
project(ResolutePulse VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 11)

# Use static linking for easier deployment
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -static-libgcc -static-libstdc++")

# Fix for MinGW "too many sections" error with debug builds of large header-only libraries
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    add_compile_options(-Wa,-mbig-obj)
endif()

# Include directories for header-only libraries
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/thirdparty)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/src)

# Find OpenSSL (required for both Agent and Manager)
find_package(OpenSSL REQUIRED)

# Build SQLite3 as a static library first
add_library(sqlite3_lib STATIC thirdparty/sqlite3/sqlite3.c)
target_include_directories(sqlite3_lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/sqlite3)

# =============================================================================
# Common sources (shared between Agent and Manager via include paths)
# =============================================================================
set(COMMON_HEADERS
    src/common/Protocol.h
)

# =============================================================================
# Agent Executable
# =============================================================================
set(AGENT_SOURCES
    src/main.cpp
    src/Agent.cpp
    src/config/ConfigManager.cpp
    src/collector/ChannelDiscovery.cpp
    src/collector/EventCollector.cpp
    src/collector/Event.cpp
    src/buffer/EventBuffer.cpp
    src/queue/EventQueue.cpp
    src/network/TcpSender.cpp
    src/sender/BatchSender.cpp
    src/service/ServiceMain.cpp
    src/utils/Logger.cpp
    src/utils/Base64.cpp
    # FIM module
    src/fim/FimDatabase.cpp
    src/fim/FileHasher.cpp
    src/fim/BaselineScanner.cpp
    src/fim/UsnJournalReader.cpp
    src/fim/FimMonitor.cpp
    # System Info module
    src/sysinfo/SystemInfoCollector.cpp
    src/sysinfo/OSInfoCollector.cpp
    src/sysinfo/InstalledAppsCollector.cpp
    src/sysinfo/OpenPortsCollector.cpp
    # Registration module
    src/agent/registration/CertificateStore.cpp
    src/agent/registration/RegistrationClient.cpp
    # TLS Sender module
    src/agent/network/TlsSender.cpp
)

set(AGENT_HEADERS
    src/Agent.h
    src/config/ConfigManager.h
    src/collector/ChannelDiscovery.h
    src/collector/EventCollector.h
    src/collector/Event.h
    src/buffer/EventBuffer.h
    src/queue/EventQueue.h
    src/network/TcpSender.h
    src/sender/BatchSender.h
    src/service/ServiceMain.h
    src/utils/Logger.h
    src/utils/Base64.h
    # FIM module
    src/fim/FimDatabase.h
    src/fim/FileHasher.h
    src/fim/BaselineScanner.h
    src/fim/UsnJournalReader.h
    src/fim/FimMonitor.h
    src/fim/FimEvent.h
    # System Info module
    src/sysinfo/SystemInfoCollector.h
    src/sysinfo/OSInfoCollector.h
    src/sysinfo/InstalledAppsCollector.h
    src/sysinfo/OpenPortsCollector.h
    # Registration module
    src/agent/registration/CertificateStore.h
    src/agent/registration/RegistrationClient.h
    src/agent/network/TlsSender.h
    # Common
    ${COMMON_HEADERS}
)

add_executable(${PROJECT_NAME} ${AGENT_SOURCES} ${AGENT_HEADERS})

target_include_directories(${PROJECT_NAME} PRIVATE 
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty
    ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/sqlite3
)

# Windows-specific definitions
target_compile_definitions(${PROJECT_NAME} PRIVATE
    _WIN32_WINNT=0x0602
    NOMINMAX
    WIN32_LEAN_AND_MEAN
    SPDLOG_HEADER_ONLY
    FMT_HEADER_ONLY
)

# Link libraries for Agent
target_link_libraries(${PROJECT_NAME} PRIVATE
    sqlite3_lib
    OpenSSL::SSL
    OpenSSL::Crypto
    wevtapi
    advapi32
    ws2_32
    bcrypt
    iphlpapi
    netapi32
)

# =============================================================================
# Manager Executable
# =============================================================================

# Find PostgreSQL
find_library(LIBPQ_LIBRARY NAMES pq libpq HINTS "C:/Program Files/PostgreSQL/*/lib")
find_path(LIBPQ_INCLUDE_DIR libpq-fe.h HINTS "C:/Program Files/PostgreSQL/*/include")

if(LIBPQ_LIBRARY AND LIBPQ_INCLUDE_DIR)
    set(MANAGER_SOURCES
        src/manager/main.cpp
        src/manager/ca/CertificateAuthority.cpp
        src/manager/db/PostgresClient.cpp
        src/manager/server/ManagerServer.cpp
        src/manager/server/AgentHandler.cpp
        src/manager/registry/AgentRegistry.cpp
        src/manager/registry/LicenseManager.cpp
        src/utils/Logger.cpp
    )

    set(MANAGER_HEADERS
        src/manager/ca/CertificateAuthority.h
        src/manager/db/PostgresClient.h
        src/manager/server/ManagerServer.h
        src/manager/server/AgentHandler.h
        src/manager/registry/AgentRegistry.h
        src/manager/registry/LicenseManager.h
        src/utils/Logger.h
        ${COMMON_HEADERS}
    )

    add_executable(ResolutePulseManager ${MANAGER_SOURCES} ${MANAGER_HEADERS})

    target_include_directories(ResolutePulseManager PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty
        ${LIBPQ_INCLUDE_DIR}
    )

    target_compile_definitions(ResolutePulseManager PRIVATE
        _WIN32_WINNT=0x0602
        NOMINMAX
        WIN32_LEAN_AND_MEAN
        SPDLOG_HEADER_ONLY
        FMT_HEADER_ONLY
    )

    target_link_libraries(ResolutePulseManager PRIVATE
        OpenSSL::SSL
        OpenSSL::Crypto
        ${LIBPQ_LIBRARY}
        ws2_32
        advapi32
    )

    message(STATUS "PostgreSQL found - Manager target enabled")
    message(STATUS "  libpq: ${LIBPQ_LIBRARY}")
    message(STATUS "  include: ${LIBPQ_INCLUDE_DIR}")
else()
    message(STATUS "PostgreSQL not found - Manager target disabled")
    message(STATUS "  Install PostgreSQL and ensure libpq is available to build the Manager")
endif()

# =============================================================================
# Test: Certificate Authority
# =============================================================================
add_executable(test_ca
    tests/test_ca.cpp
    src/manager/ca/CertificateAuthority.cpp
    src/utils/Logger.cpp
)

target_include_directories(test_ca PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty
)

target_compile_definitions(test_ca PRIVATE
    _WIN32_WINNT=0x0602
    NOMINMAX
    WIN32_LEAN_AND_MEAN
    SPDLOG_HEADER_ONLY
    FMT_HEADER_ONLY
)

target_link_libraries(test_ca PRIVATE
    OpenSSL::SSL
    OpenSSL::Crypto
    ws2_32
)

# =============================================================================
# Test: Postgres Client (Mocked)
# =============================================================================
add_executable(test_postgres_client
    tests/test_postgres_client.cpp
    src/manager/db/PostgresClient.cpp
    src/utils/Logger.cpp
)

target_include_directories(test_postgres_client PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty
    ${LIBPQ_INCLUDE_DIR}
)

target_compile_definitions(test_postgres_client PRIVATE
    _WIN32_WINNT=0x0602
    NOMINMAX
    WIN32_LEAN_AND_MEAN
    SPDLOG_HEADER_ONLY
    FMT_HEADER_ONLY
)

# NOTE: We specifically DO NOT link ${LIBPQ_LIBRARY} here 
# because we provide our own mock in test_postgres_client.cpp
target_link_libraries(test_postgres_client PRIVATE
    ws2_32
    advapi32
)

# =============================================================================
# Install targets
# =============================================================================
install(TARGETS ${PROJECT_NAME} DESTINATION bin)
install(FILES config.json DESTINATION bin)

if(TARGET ResolutePulseManager)
    install(TARGETS ResolutePulseManager DESTINATION bin)
endif()
