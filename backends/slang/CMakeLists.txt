# CMakeLists.txt for Slang integration test
cmake_minimum_required(VERSION 3.10)
project(SlangIntegrationTest)

# Add path to custom FindSlang.cmake
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/cmake")

find_package(Slang REQUIRED)

add_executable(slang_test slang_test.cpp)
target_include_directories(slang_test PRIVATE ${Slang_INCLUDE_DIRS})
target_link_libraries(slang_test PRIVATE Slang::Slang)

# Copy the correct slang.dll and slang-gfx.dll to the output directory after build
if(WIN32)
    set(SLANG_DLLS
        "${Slang_ROOT}/bin/slang.dll"
    )
    foreach(DLL ${SLANG_DLLS})
        add_custom_command(TARGET slang_test POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${DLL}"
                $<TARGET_FILE_DIR:slang_test>
            COMMENT "Copying ${DLL} to output directory"
        )
    endforeach()
endif()
