cmake_minimum_required(VERSION 3.15)
project(IFDownload VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(ifd
    src/main.cpp
    src/http.cpp
    src/hash.cpp
    src/manifest.cpp
    src/signature.cpp
    src/cache.cpp
    src/tunnel.cpp
)

target_include_directories(ifd PRIVATE src third)

if(WIN32)
    target_link_libraries(ifd PRIVATE
        winhttp
        bcrypt
        shell32
        wintrust
        ole32
        uuid
        ws2_32
    )
    if(MSVC)
        target_compile_options(ifd PRIVATE /utf-8 /W4 /O2 /D_CRT_SECURE_NO_WARNINGS)
    else()
        target_compile_options(ifd PRIVATE -Wall -Wextra -O2)
    endif()
else()
    find_package(CURL REQUIRED)
    find_package(OpenSSL REQUIRED)

    target_link_libraries(ifd PRIVATE
        CURL::libcurl
        OpenSSL::Crypto
        pthread
    )
    target_compile_options(ifd PRIVATE -Wall -Wextra -O2)
endif()

add_custom_command(TARGET ifd POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    "${CMAKE_SOURCE_DIR}/manifest"
    "$<TARGET_FILE_DIR:ifd>/manifest"
)
