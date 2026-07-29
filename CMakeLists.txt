cmake_minimum_required(VERSION 3.10)
project(CoTetris VERSION 1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Build everything (SDL, SDL_image, glm) as static libraries so we don't
# have to ship/copy DLLs next to the executable on Windows.
set(BUILD_SHARED_LIBS OFF)
set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON CACHE BOOL "" FORCE)
set(SDLIMAGE_SHARED OFF CACHE BOOL "" FORCE)
set(SDLIMAGE_STATIC ON CACHE BOOL "" FORCE)

add_subdirectory(libs/SDL)
add_subdirectory(libs/SDL_image)
add_subdirectory(libs/SDL_net)
add_subdirectory(libs/GLAD)
add_subdirectory(libs/GLM)
# add_subdirectory(libs/FreeType)

add_executable(${PROJECT_NAME}
    src/tinyfiledialogs/tinyfiledialogs.c
    src/main.cpp
    src/core/sdl.cpp
    src/core/gl.cpp
)

target_link_libraries(${PROJECT_NAME}
    PRIVATE
        SDL3::SDL3-static
        SDL3_image::SDL3_image-static
        SDL3_net::SDL3_net-static
        GLAD
        glm::glm
        # freetype
)

if (WIN32)
    # tinyfiledialogs' native Windows dialogs go through COM (CoCreateInstance)
    # and the legacy comdlg32 file-dialog API, neither of which MSVC links by default.
    target_link_libraries(${PROJECT_NAME} PRIVATE comdlg32 ole32 uuid)
endif()

target_include_directories(${PROJECT_NAME}
    PRIVATE
        libs/SDL/include
        libs/SDL_image/include
        libs/SDL_net/include
        libs/GLAD/include
        libs/GLM
        src/tinyfiledialogs
        # libs/FreeType/include
)
