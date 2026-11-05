# Generators of IABLib renderer configurations (.cfg) for real rooms, on top of IABLib's own transform. Not a project of
# its own: added with add_subdirectory after IABLib (the top CMakeLists.txt does).

if(NOT TARGET IABLib)
    message(FATAL_ERROR "layouts/ must be added after IABLib")
endif()
if(NOT IABLib_HEADER_PATH)
    set(IABLib_HEADER_PATH ${CMAKE_CURRENT_SOURCE_DIR}/../include ${CMAKE_CURRENT_SOURCE_DIR}/../src/lib)
endif()

add_executable(cubeangles
    ${CMAKE_CURRENT_SOURCE_DIR}/cubeangles.cpp
)
target_include_directories(cubeangles PRIVATE
    ${IABLib_HEADER_PATH}
)
target_link_libraries(cubeangles
    IABLib
)
