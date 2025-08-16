# Add source to this project's executable.
add_executable (Minerva
  main.cpp
  vk_initializers.cpp
  vk_images.cpp 
  vk_descriptors.cpp
  vk_pipelines.cpp
  vk_engine.cpp
  vk_loader.cpp
  camera.cpp
)

set_property(TARGET Minerva PROPERTY CXX_STANDARD 20)
target_compile_definitions(Minerva PUBLIC
  GLM_FORCE_DEPTH_ZERO_TO_ONE
)
target_include_directories(Minerva PUBLIC
  "${CMAKE_CURRENT_SOURCE_DIR}"
)

target_link_libraries(Minerva PUBLIC
  vma
  glm
  Vulkan::Vulkan
  fmt::fmt
  stb_image
  SDL2::SDL2
  vkbootstrap
  imgui
  fastgltf::fastgltf
)

target_precompile_headers(Minerva PUBLIC
  <optional>
  <vector>
  <memory>
  <string>
  <unordered_map>
  <glm/mat4x4.hpp>
  <glm/vec4.hpp>
  <vulkan/vulkan.h>
)

add_custom_command(TARGET Minerva POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_RUNTIME_DLLS:Minerva> $<TARGET_FILE_DIR:Minerva>
  COMMAND_EXPAND_LISTS
)