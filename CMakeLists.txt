cmake_minimum_required(VERSION 3.10)
project(CoTetris VERSION 1.0)
set(CMAKE_CXX_STANDARD 17)

add_subdirectory(libs/SDL)
add_subdirectory(libs/SDL_image)
add_subdirectory(libs/GLAD)
add_subdirectory(libs/GLM)
# add_subdirectory(libs/FreeType)
add_executable(${PROJECT_NAME}
    src/main.cpp
    src/core/sdl.cpp
    src/core/gl.cpp
)
target_link_libraries(${PROJECT_NAME} SDL3::SDL3 SDL3_image::SDL3_image GLAD glm::glm
    # freetype
)
target_include_directories(${PROJECT_NAME} PRIVATE
    libs/SDL/include
    libs/SDL_image/include
    libs/GLAD/include
    libs/GLM
    # libs/FreeType/include
)