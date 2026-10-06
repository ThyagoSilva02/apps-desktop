cmake_minimum_required(VERSION 3.20)
project(MacroPillControl VERSION 1.0.0 LANGUAGES CXX RC)

if(NOT WIN32)
    message(FATAL_ERROR "MacroPill Control usa a API Win32 e deve ser compilado para Windows.")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(THREADS_PREFER_PTHREAD_FLAG TRUE)
find_package(Threads REQUIRED)

if(MSVC)
    # O executável não depende da instalação do Visual C++ Redistributable.
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

set(MACROPILL_LIBRARIES
    Threads::Threads
    comctl32 comdlg32 shell32 ole32 uuid gdi32 user32 advapi32 uxtheme dwmapi
)

function(macropill_configure target)
    target_include_directories(${target} PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
        "${CMAKE_CURRENT_SOURCE_DIR}"
    )
    target_compile_definitions(${target} PRIVATE
        UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN
        WINVER=0x0A00 _WIN32_WINNT=0x0A00
    )
    target_link_libraries(${target} PRIVATE ${MACROPILL_LIBRARIES})
    if(MSVC)
        target_compile_options(${target} PRIVATE
            $<$<COMPILE_LANGUAGE:CXX>:/W4>
            $<$<COMPILE_LANGUAGE:CXX>:/permissive->
            $<$<COMPILE_LANGUAGE:CXX>:/utf-8>
            $<$<COMPILE_LANGUAGE:CXX>:/EHsc>
        )
    else()
        target_compile_options(${target} PRIVATE
            $<$<COMPILE_LANGUAGE:CXX>:-Wall>
            $<$<COMPILE_LANGUAGE:CXX>:-Wextra>
            $<$<COMPILE_LANGUAGE:CXX>:-Wpedantic>
        )
    endif()
endfunction()

add_executable(MacroPillControl WIN32
    src/main.cpp
    src/config.cpp
    src/actions.cpp
    src/serial.cpp
    app.rc
)
macropill_configure(MacroPillControl)

# O manifesto é incluído em app.rc para funcionar com MSVC e MinGW.
if(MSVC)
    target_link_options(MacroPillControl PRIVATE /MANIFEST:NO)
elseif(MINGW)
    target_link_options(MacroPillControl PRIVATE -municode -static)
endif()

set_target_properties(MacroPillControl PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/bin"
)
foreach(configuration Debug Release RelWithDebInfo MinSizeRel)
    string(TOUPPER "${configuration}" configuration_upper)
    set_target_properties(MacroPillControl PROPERTIES
        "RUNTIME_OUTPUT_DIRECTORY_${configuration_upper}" "${CMAKE_CURRENT_SOURCE_DIR}/bin"
    )
endforeach()

include(CTest)
if(BUILD_TESTING)
    foreach(test_name core_tests serial_tests ui_tests)
        if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/${test_name}.cpp")
            add_executable(${test_name}
                tests/${test_name}.cpp
                src/config.cpp
                src/actions.cpp
                src/serial.cpp
            )
            macropill_configure(${test_name})
            if(MINGW)
                target_link_options(${test_name} PRIVATE -static)
            endif()
            add_test(NAME ${test_name} COMMAND ${test_name})
        endif()
    endforeach()
endif()
