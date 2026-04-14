cmake_minimum_required(VERSION 3.20)
project(ResolutePulseManager VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Include directories for header-only libraries
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/src)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/thirdparty)

# Find dependencies
find_package(OpenSSL REQUIRED)
find_package(PostgreSQL REQUIRED)

# =============================================================================
# Manager Sources
# =============================================================================
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
    src/common/Protocol.h
)

add_executable(ResolutePulseManager ${MANAGER_SOURCES} ${MANAGER_HEADERS})

target_include_directories(ResolutePulseManager PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty
    ${PostgreSQL_INCLUDE_DIRS}
)

target_compile_definitions(ResolutePulseManager PRIVATE
    SPDLOG_HEADER_ONLY
    FMT_HEADER_ONLY
)

target_link_libraries(ResolutePulseManager PRIVATE
    OpenSSL::SSL
    OpenSSL::Crypto
    PostgreSQL::PostgreSQL
    pthread
)

# Install
install(TARGETS ResolutePulseManager DESTINATION bin)
