add_library(utils STATIC
        http_get.cpp
        http_get.hpp
        blacklist.cpp
        blacklist.hpp
        make_endpoint.hpp
        make_endpoint.cpp
        bytes_to_debug_string.cpp
        bytes_to_debug_string.hpp
        crypto.cpp
        crypto.hpp)

target_link_libraries(utils ${CURL_LIBRARIES} ${JSONCPP_LIBRARIES} ${OPENSSL_LIBRARIES})

add_dependencies(utils boost jsoncpp openssl)
target_include_directories(utils SYSTEM PRIVATE ${JSONCPP_INCLUDE_DIRS})

add_subdirectory(test)
