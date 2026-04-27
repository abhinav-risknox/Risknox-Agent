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

# Default to Release build (smaller, faster binaries for deployment)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
    message(STATUS "Build type not specified - defaulting to Release")
endif()
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")

# Release flags: full optimisation + strip debug info at link time
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    add_compile_options(-O2)
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -s")  # -s strips symbols
endif()

# Helper: add a post-build strip step to any target (no-op on non-GNU toolchains)
function(strip_target TARGET_NAME)
    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND CMAKE_BUILD_TYPE STREQUAL "Release")
        find_program(STRIP_EXE strip)
        if(STRIP_EXE)
            add_custom_command(TARGET ${TARGET_NAME} POST_BUILD
                COMMAND ${STRIP_EXE} $<TARGET_FILE:${TARGET_NAME}>
                COMMENT "Stripping debug symbols from ${TARGET_NAME}")
        endif()
    endif()
endfunction()

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
    # IPC layer (Named Pipe channel to worker processes)
    src/ipc/PipeChannel.cpp
    # Worker process manager
    src/workers/WorkerManager.cpp
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
    # Policy module
    src/policy/PolicyManager.h
    src/policy/CommandQueue.h
    # Patch Management module
    src/patch/PatchManager.h
    # Web Blocking module
    src/webblock/WebBlocker.h
    # Software Blocking module
    src/appblock/SoftwareBlocker.h
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
    ole32
    oleaut32
)

install(TARGETS ${PROJECT_NAME} DESTINATION bin)
strip_target(${PROJECT_NAME})

# =============================================================================
# Manager Executable
# =============================================================================

# Find PostgreSQL
set(PG_ROOT "C:/Program Files/PostgreSQL/18")
find_path(LIBPQ_INCLUDE_DIR libpq-fe.h HINTS "${PG_ROOT}/include")
set(LIBPQ_DLL "${PG_ROOT}/bin/libpq.dll")

if(EXISTS "${LIBPQ_DLL}" AND LIBPQ_INCLUDE_DIR)
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
        ${LIBPQ_DLL}
        ws2_32
        advapi32
        secur32
        crypt32
    )

    message(STATUS "PostgreSQL found - Manager target enabled (DLL linking)")
    message(STATUS "  libpq DLL: ${LIBPQ_DLL}")
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
# Test: Manager Server (Integration - mock libpq, real CA + TLS)
# =============================================================================
add_executable(test_manager_server
    tests/test_manager_server.cpp
    src/manager/server/ManagerServer.cpp
    src/manager/server/AgentHandler.cpp
    src/manager/ca/CertificateAuthority.cpp
    src/manager/db/PostgresClient.cpp
    src/utils/Logger.cpp
)

target_include_directories(test_manager_server PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty
    ${LIBPQ_INCLUDE_DIR}
)

target_compile_definitions(test_manager_server PRIVATE
    _WIN32_WINNT=0x0602
    NOMINMAX
    WIN32_LEAN_AND_MEAN
    SPDLOG_HEADER_ONLY
    FMT_HEADER_ONLY
)

target_link_libraries(test_manager_server PRIVATE
    OpenSSL::SSL
    OpenSSL::Crypto
    ws2_32
    advapi32
)

# =============================================================================
# Test: Agent Registry (Unit - mock libpq)
# =============================================================================
add_executable(test_agent_registry
    tests/test_agent_registry.cpp
    src/manager/registry/AgentRegistry.cpp
    src/manager/db/PostgresClient.cpp
    src/utils/Logger.cpp
)

target_include_directories(test_agent_registry PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty
    ${LIBPQ_INCLUDE_DIR}
)

target_compile_definitions(test_agent_registry PRIVATE
    _WIN32_WINNT=0x0602 NOMINMAX WIN32_LEAN_AND_MEAN SPDLOG_HEADER_ONLY FMT_HEADER_ONLY
)

target_link_libraries(test_agent_registry PRIVATE ws2_32 advapi32)

# =============================================================================
# Test: License Manager (Unit - mock libpq)
# =============================================================================
add_executable(test_license_manager
    tests/test_license_manager.cpp
    src/manager/registry/LicenseManager.cpp
    src/manager/db/PostgresClient.cpp
    src/utils/Logger.cpp
)

target_include_directories(test_license_manager PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty
    ${LIBPQ_INCLUDE_DIR}
)

target_compile_definitions(test_license_manager PRIVATE
    _WIN32_WINNT=0x0602 NOMINMAX WIN32_LEAN_AND_MEAN SPDLOG_HEADER_ONLY FMT_HEADER_ONLY
)

target_link_libraries(test_license_manager PRIVATE ws2_32 advapi32)

# =============================================================================
# Test: Agent Handler (Integration - mock libpq, real CA + OpenSSL)
# =============================================================================
add_executable(test_agent_handler
    tests/test_agent_handler.cpp
    src/manager/server/AgentHandler.cpp
    src/manager/ca/CertificateAuthority.cpp
    src/manager/db/PostgresClient.cpp
    src/utils/Logger.cpp
)

target_include_directories(test_agent_handler PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty
    ${LIBPQ_INCLUDE_DIR}
)

target_compile_definitions(test_agent_handler PRIVATE
    _WIN32_WINNT=0x0602 NOMINMAX WIN32_LEAN_AND_MEAN SPDLOG_HEADER_ONLY FMT_HEADER_ONLY
)

target_link_libraries(test_agent_handler PRIVATE
    OpenSSL::SSL
    OpenSSL::Crypto
    ws2_32
    advapi32
)

# =============================================================================
# Test: Agent TLS (Unit + Integration - CertificateStore, RegistrationClient, TlsSender)
# =============================================================================
add_executable(test_agent_tls
    tests/test_agent_tls.cpp
    src/agent/registration/CertificateStore.cpp
    src/agent/registration/RegistrationClient.cpp
    src/agent/network/TlsSender.cpp
    src/manager/ca/CertificateAuthority.cpp
    src/utils/Logger.cpp
    src/utils/Base64.cpp
)

target_include_directories(test_agent_tls PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty
)

target_compile_definitions(test_agent_tls PRIVATE
    _WIN32_WINNT=0x0602
    NOMINMAX
    WIN32_LEAN_AND_MEAN
    SPDLOG_HEADER_ONLY
    FMT_HEADER_ONLY
)

target_link_libraries(test_agent_tls PRIVATE
    OpenSSL::SSL
    OpenSSL::Crypto
    ws2_32
    advapi32
    crypt32
    secur32
)

# =============================================================================
# Verification: Licenses
# =============================================================================
add_executable(verify_licenses
    tests/verify_licenses.cpp
    src/manager/db/PostgresClient.cpp
    src/utils/Logger.cpp
)

target_include_directories(verify_licenses PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty
    ${LIBPQ_INCLUDE_DIR}
)

target_compile_definitions(verify_licenses PRIVATE
    _WIN32_WINNT=0x0602 NOMINMAX WIN32_LEAN_AND_MEAN SPDLOG_HEADER_ONLY FMT_HEADER_ONLY
)

target_link_libraries(verify_licenses PRIVATE
    ${LIBPQ_DLL}
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

# =============================================================================
# Worker executable targets (Wazuh-style child processes)
# Each is a separate lightweight exe, called by the core agent via Named Pipe.
# =============================================================================

# Shared compile options for all workers
set(WORKER_COMPILE_DEFS
    _WIN32_WINNT=0x0602
    NOMINMAX
    WIN32_LEAN_AND_MEAN
    SPDLOG_HEADER_ONLY
    FMT_HEADER_ONLY
)

# ── rp-softblock.exe ──────────────────────────────────────────────────────────
add_executable(rp-softblock
    src/workers/SoftwareBlockerWorker.cpp
    src/appblock/SoftwareBlocker.cpp
    src/ipc/PipeChannel.cpp
    src/config/ConfigManager.cpp
    src/utils/Logger.cpp
)
target_include_directories(rp-softblock PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty
)
target_compile_definitions(rp-softblock PRIVATE ${WORKER_COMPILE_DEFS})
target_link_libraries(rp-softblock PRIVATE ws2_32 advapi32)
install(TARGETS rp-softblock DESTINATION bin)
strip_target(rp-softblock)

# ── rp-webblock.exe ───────────────────────────────────────────────────────────
add_executable(rp-webblock
    src/workers/WebBlockerWorker.cpp
    src/webblock/WebBlocker.cpp
    src/ipc/PipeChannel.cpp
    src/config/ConfigManager.cpp
    src/utils/Logger.cpp
)
target_include_directories(rp-webblock PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty
)
target_compile_definitions(rp-webblock PRIVATE ${WORKER_COMPILE_DEFS})
target_link_libraries(rp-webblock PRIVATE ws2_32 advapi32)
install(TARGETS rp-webblock DESTINATION bin)
strip_target(rp-webblock)

# ── rp-patch.exe ──────────────────────────────────────────────────────────────
add_executable(rp-patch
    src/workers/PatchWorker.cpp
    src/patch/PatchManager.cpp
    src/ipc/PipeChannel.cpp
    src/config/ConfigManager.cpp
    src/utils/Logger.cpp
)
target_include_directories(rp-patch PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty
)
target_compile_definitions(rp-patch PRIVATE ${WORKER_COMPILE_DEFS})
target_link_libraries(rp-patch PRIVATE ws2_32 advapi32 ole32 oleaut32)
install(TARGETS rp-patch DESTINATION bin)
strip_target(rp-patch)

# ── rp-antivirus.exe ──────────────────────────────────────────────────────────
add_executable(rp-antivirus
    src/antivirus/AntivirusWorker.cpp
    src/ipc/PipeChannel.cpp
    src/utils/Logger.cpp
)
target_include_directories(rp-antivirus PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty
)
target_compile_definitions(rp-antivirus PRIVATE ${WORKER_COMPILE_DEFS})
target_link_libraries(rp-antivirus PRIVATE ws2_32 advapi32)
install(TARGETS rp-antivirus DESTINATION bin)
strip_target(rp-antivirus)

