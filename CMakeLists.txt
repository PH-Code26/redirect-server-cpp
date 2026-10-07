cmake_minimum_required(VERSION 3.16)
project(url-alias-redirect VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

include(FetchContent)

FetchContent_Declare(
    cpp-httplib
    GIT_REPOSITORY https://gitee.com/mirrors/cpp-httplib.git  
    GIT_TAG        v0.18.3
)
FetchContent_MakeAvailable(cpp-httplib)

FetchContent_Declare(
    json
    GIT_REPOSITORY https://gitee.com/mirrors/nlohmann-json.git
    GIT_TAG        v3.11.3
)
FetchContent_MakeAvailable(json)

find_package(OpenSSL QUIET)
if(NOT OpenSSL_FOUND)
    message(WARNING "OpenSSL not found — HTTPS will be disabled. Install OpenSSL or use vcpkg/pacman.")
endif()

add_executable(url-alias-redirect src/server.cpp)

target_link_libraries(url-alias-redirect PRIVATE httplib::httplib nlohmann_json::nlohmann_json)

if(OpenSSL_FOUND)
    target_compile_definitions(url-alias-redirect PRIVATE CPPHTTPLIB_OPENSSL_SUPPORT)
    target_link_libraries(url-alias-redirect PRIVATE OpenSSL::SSL OpenSSL::Crypto)
endif()

if(WIN32)
    target_link_libraries(url-alias-redirect PRIVATE ws2_32 crypt32)
    set_target_properties(url-alias-redirect PROPERTIES
        WIN32_EXECUTABLE OFF
    )
endif()

install(TARGETS url-alias-redirect RUNTIME DESTINATION .)