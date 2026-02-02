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

# Find OpenSSL
find_package(OpenSSL QUIET)

# Build SQLite3 as a static library first
add_library(sqlite3_lib STATIC thirdparty/sqlite3/sqlite3.c)
target_include_directories(sqlite3_lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/sqlite3)

# Source files (C++ only)
set(SOURCES
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
)

# Header files
set(HEADERS
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
)

# Main executable
add_executable(${PROJECT_NAME} ${SOURCES} ${HEADERS})

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

# Link libraries
target_link_libraries(${PROJECT_NAME} PRIVATE
    sqlite3_lib
    wevtapi
    advapi32
    ws2_32
    bcrypt      # For SHA256 hashing in FIM
)

# Add OpenSSL if found (for HTTPS support)
if(OpenSSL_FOUND)
    target_compile_definitions(${PROJECT_NAME} PRIVATE CPPHTTPLIB_OPENSSL_SUPPORT)
    target_link_libraries(${PROJECT_NAME} PRIVATE OpenSSL::SSL OpenSSL::Crypto)
    message(STATUS "OpenSSL found - HTTPS enabled")
else()
    message(STATUS "OpenSSL not found - HTTP only mode")
endif()

# Install target
install(TARGETS ${PROJECT_NAME} DESTINATION bin)
install(FILES config.json DESTINATION bin)
