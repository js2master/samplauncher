# Host-side native unit tests (protocol, pure core logic, open.mp interop)
# and netcap CLI utilities. Built on PC only; never packaged into samp.apk.
cmake_minimum_required(VERSION 3.22)
project(samp_native_tests CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
enable_testing()

get_filename_component(REPO "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
set(CLIENT "${REPO}/app/src/main/cpp/SAMPMOBILE")
set(RAKNET "${CLIENT}/Deps/RakNet")
set(NETCAP_TOOLS "${REPO}/tools/netcap")

# Shared RakNet wire primitives compiled directly from client source.
add_library(rakbits STATIC
        "${RAKNET}/RakNet/BitStream.cpp"
        "${RAKNET}/RakNet/StringCompressor.cpp"
        "${RAKNET}/RakNet/DS_HuffmanEncodingTree.cpp"
        "${RAKNET}/RakNet/SAMP/samp_auth.cpp"
        "${RAKNET}/RakNet/SAMP/samp_netencr.cpp")
target_include_directories(rakbits PUBLIC "${RAKNET}" PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/shim")
target_compile_options(rakbits PRIVATE -w)

function(add_native_test name src)
    add_executable(test_${name} "${src}")
    target_include_directories(test_${name} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}" "${CLIENT}")
    if (ARGN)
        target_link_libraries(test_${name} PRIVATE ${ARGN})
    endif ()
    add_test(NAME ${name} COMMAND test_${name})
endfunction()

# ---- Core pure logic tests (no engine dependency) ----
add_native_test(input_arbiter      core/test_input_arbiter.cpp)
add_native_test(settings_contract  core/test_settings_contract.cpp)

add_library(artwork_core STATIC
        "${CLIENT}/Game/Artwork/ArtworkCore.cpp"
        "${CLIENT}/Game/Artwork/TxdParser.cpp"
        "${CLIENT}/Game/Artwork/DffValidator.cpp"
        "${CLIENT}/Game/Artwork/Lzma1Decoder.cpp")
target_include_directories(artwork_core PUBLIC "${CLIENT}")
add_native_test(artwork_core       core/test_artwork_core.cpp       artwork_core)

# ---- Protocol & netcap format tests ----
add_native_test(bitstream          proto/test_bitstream.cpp         rakbits)
add_native_test(netcap             proto/test_netcap.cpp)
add_native_test(wire               proto/test_wire.cpp              rakbits)
add_native_test(rpc_batch1         proto/test_rpc_batch1.cpp        rakbits)
add_native_test(rpc_batch2         proto/test_rpc_batch2.cpp        rakbits)
add_native_test(rpc_batch3         proto/test_rpc_batch3.cpp        rakbits)
add_native_test(rpc_batch4         proto/test_rpc_batch4.cpp        rakbits)
add_native_test(rpc_batch5         proto/test_rpc_batch5.cpp        rakbits)
add_native_test(sync_batch6        proto/test_sync_batch6.cpp       rakbits)
add_native_test(outbound_batch7    proto/test_outbound_batch7.cpp   rakbits)

# ---- Netcap diagnostic CLI tools (kept compiling with Net/Proto) ----
add_executable(ncapdump "${NETCAP_TOOLS}/ncapdump.cpp")
target_include_directories(ncapdump PRIVATE "${CLIENT}")

add_executable(ncapreplay "${NETCAP_TOOLS}/ncapreplay.cpp")
target_include_directories(ncapreplay PRIVATE "${CLIENT}")
target_link_libraries(ncapreplay PRIVATE rakbits)

# ---- open.mp server oracle interop tests (optional local reference) ----
set(OMP_DIR "${REPO}/reference/open.mp 上游源码" CACHE PATH "open.mp server source tree")
if (EXISTS "${OMP_DIR}/Shared/NetCode/netcode.hpp")
    file(GLOB OMP_ENCODING "${OMP_DIR}/Shared/Network/Encoding/*.cpp")
    add_library(omp_oracle STATIC "${OMP_DIR}/Shared/Network/bitstream.cpp" ${OMP_ENCODING})
    target_include_directories(omp_oracle SYSTEM PUBLIC
            "${OMP_DIR}/SDK/include"
            "${OMP_DIR}/SDK/lib/glm"
            "${OMP_DIR}/SDK/lib/robin-hood-hashing/src/include"
            "${OMP_DIR}/SDK/lib/span-lite/include"
            "${OMP_DIR}/SDK/lib/string-view-lite/include"
            "${OMP_DIR}/Shared/Network"
            "${OMP_DIR}/Shared/NetCode")
    target_compile_options(omp_oracle PRIVATE -w)
    set_target_properties(omp_oracle PROPERTIES CXX_STANDARD 17)

    foreach(omp_name IN ITEMS omp_interop omp_batch1 omp_batch2 omp_batch3 omp_batch4 omp_batch5 omp_batch6 omp_batch7 omp_artwork)
        add_native_test(${omp_name} "interop/test_${omp_name}.cpp" rakbits omp_oracle)
        set_target_properties(test_${omp_name} PROPERTIES CXX_STANDARD 17)
    endforeach()

    message(STATUS "open.mp oracle found: interop tests enabled")
else ()
    message(STATUS "open.mp oracle not found at ${OMP_DIR}: interop tests skipped")
endif ()
