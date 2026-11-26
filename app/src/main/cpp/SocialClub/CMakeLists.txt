project(SCAnd)

set(CMAKE_CXX_STANDARD 20)

file(GLOB INCLUDE_FILES
        *.h
)

file(GLOB SOURCE_FILES
        *.cpp
)

add_library(${PROJECT_NAME} SHARED
        ${INCLUDE_FILES}
        ${SOURCE_FILES}
)

target_link_libraries(${PROJECT_NAME} PRIVATE
        log
)

target_compile_options(${PROJECT_NAME} PRIVATE
        -fvisibility=default        # Export all symbols
        -Werror=return-type
        -Werror=return-stack-address
        -Werror=mismatched-new-delete
        -Werror=varargs
        -Wuninitialized
        -Wformat
        -Wformat-security
        -Wint-to-pointer-cast
        -Wpointer-to-int-cast
        -Wsizeof-pointer-memaccess
        -Warray-bounds
        -Wliteral-conversion
        -Wpointer-bool-conversion
        -Wparentheses
        -Wnonportable-include-path
)

set(CMAKE_BUILD_TYPE Release)

set_property(TARGET ${PROJECT_NAME} PROPERTY ENABLE_EXPORTS ON)
