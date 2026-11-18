cmake_minimum_required(VERSION 3.20)
project(VPPGraphics LANGUAGES C ASM)
# C chỉ dùng để đối chiếu ABI với header SDK và liên kết CRT.
include(CTest)
option(VPP_GRAPHICS_SDL "Dựng cửa sổ và OpenGL SDL2" ON)
option(VPP_GRAPHICS_VULKAN "Dựng Vulkan" ON)
if(NOT CMAKE_SYSTEM_NAME MATCHES "^(Linux|Windows)$" OR
   NOT CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64|amd64)$")
  message(FATAL_ERROR "Trợ giúp SDL/Vulkan hiện hỗ trợ Linux và Windows x86-64")
endif()
if(MSVC)
  message(FATAL_ERROR "Dùng GCC hoặc Clang với ABI GNU (MinGW trên Windows)")
endif()
if(BUILD_TESTING)
  find_package(Python3 REQUIRED COMPONENTS Interpreter)
endif()
include(CheckCSourceCompiles)
set(ASM_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../ASM/common/graphics")
if(VPP_GRAPHICS_SDL)
  find_package(SDL2 CONFIG QUIET)
  if(NOT TARGET SDL2::SDL2)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(SDL2 REQUIRED IMPORTED_TARGET sdl2)
    set(SDL_LIB PkgConfig::SDL2)
  else()
    set(SDL_LIB SDL2::SDL2)
  endif()
  set(CMAKE_REQUIRED_LIBRARIES ${SDL_LIB})
  check_c_source_compiles([=[
#define SDL_MAIN_HANDLED
#include <stddef.h>
#include <SDL.h>
_Static_assert(sizeof(SDL_Event)==56, "SDL_Event");
_Static_assert(offsetof(SDL_KeyboardEvent,keysym.sym)==20, "SDL keycode");
_Static_assert(offsetof(SDL_WindowEvent,event)==12, "SDL window event");
_Static_assert(offsetof(SDL_MouseButtonEvent,button)==16, "SDL button");
_Static_assert(offsetof(SDL_MouseButtonEvent,x)==20 && offsetof(SDL_MouseButtonEvent,y)==24, "SDL mouse position");
_Static_assert(SDL_QUIT==0x100 && SDL_WINDOWEVENT==0x200 && SDL_WINDOWEVENT_CLOSE==14, "SDL window constants");
_Static_assert(SDL_KEYDOWN==0x300 && SDL_KEYUP==0x301 && SDL_MOUSEBUTTONDOWN==0x401 && SDL_MOUSEBUTTONUP==0x402, "SDL input constants");
_Static_assert(SDL_INIT_VIDEO==32 && SDL_WINDOWPOS_CENTERED_MASK==0x2fff0000, "SDL init");
_Static_assert(SDL_WINDOW_SHOWN==4 && SDL_WINDOW_RESIZABLE==32 && SDL_WINDOW_OPENGL==2 && SDL_WINDOW_HIDDEN==8, "SDL window flags");
_Static_assert(SDL_GL_RED_SIZE==0 && SDL_GL_GREEN_SIZE==1 && SDL_GL_BLUE_SIZE==2 && SDL_GL_ALPHA_SIZE==3, "SDL GL attributes");
int main(void) {return 0;}
]=] VPP_SDL_ABI)
  unset(CMAKE_REQUIRED_LIBRARIES)
  if(NOT VPP_SDL_ABI)
    message(FATAL_ERROR "ABI SDL2 không khớp với nguồn ASM")
  endif()
  add_executable(vpp-graphics-sdl "${ASM_DIR}/cua_so_x86_64.S")
  target_link_libraries(vpp-graphics-sdl PRIVATE ${SDL_LIB})
  if(WIN32)
    target_link_options(vpp-graphics-sdl PRIVATE -municode)
  endif()
  install(TARGETS vpp-graphics-sdl RUNTIME DESTINATION bin)
  if(BUILD_TESTING)
    add_test(NAME graphics-sdl-version COMMAND vpp-graphics-sdl --check)
    add_test(NAME graphics-sdl-window COMMAND "${Python3_EXECUTABLE}"
      "${CMAKE_CURRENT_SOURCE_DIR}/check_window.py" "$<TARGET_FILE:vpp-graphics-sdl>")
    set_tests_properties(graphics-sdl-window PROPERTIES TIMEOUT 40)
  endif()
endif()
if(VPP_GRAPHICS_VULKAN)
  find_package(Vulkan REQUIRED)
  set(CMAKE_REQUIRED_LIBRARIES Vulkan::Vulkan)
  check_c_source_compiles([=[
#include <stddef.h>
#include <vulkan/vulkan.h>
#define SIZE(t,n) _Static_assert(sizeof(t)==(n), #t)
#define OFF(t,f,n) _Static_assert(offsetof(t,f)==(n), #t "." #f)
SIZE(VkInstanceCreateInfo,64);
SIZE(VkDeviceQueueCreateInfo,40);
OFF(VkDeviceQueueCreateInfo,queueFamilyIndex,20);
OFF(VkDeviceQueueCreateInfo,pQueuePriorities,32);
SIZE(VkDeviceCreateInfo,72);
OFF(VkDeviceCreateInfo,pQueueCreateInfos,24);
SIZE(VkQueueFamilyProperties,24);
OFF(VkQueueFamilyProperties,queueCount,4);
SIZE(VkBufferCreateInfo,56);
OFF(VkBufferCreateInfo,size,24);
OFF(VkBufferCreateInfo,usage,32);
SIZE(VkImageCreateInfo,88);
OFF(VkImageCreateInfo,imageType,20);
OFF(VkImageCreateInfo,extent,28);
OFF(VkImageCreateInfo,usage,56);
OFF(VkImageCreateInfo,initialLayout,80);
SIZE(VkImageMemoryBarrier,72);
OFF(VkImageMemoryBarrier,image,40);
OFF(VkImageMemoryBarrier,subresourceRange,48);
SIZE(VkBufferImageCopy,56);
OFF(VkBufferImageCopy,imageExtent,44);
SIZE(VkImageSubresourceRange,20);
_Static_assert(VK_FORMAT_R8G8B8A8_UNORM == 37, "RGBA8");
_Static_assert(VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL == 7, "transfer dst");
_Static_assert(VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL == 6, "transfer src");
SIZE(VkMemoryRequirements,24);
OFF(VkMemoryRequirements,memoryTypeBits,16);
SIZE(VkMemoryAllocateInfo,32);
OFF(VkMemoryAllocateInfo,allocationSize,16);
OFF(VkMemoryAllocateInfo,memoryTypeIndex,24);
SIZE(VkPhysicalDeviceMemoryProperties,520);
OFF(VkPhysicalDeviceMemoryProperties,memoryTypes,4);
SIZE(VkMemoryType,8);
SIZE(VkCommandPoolCreateInfo,24);
OFF(VkCommandPoolCreateInfo,queueFamilyIndex,20);
SIZE(VkCommandBufferAllocateInfo,32);
OFF(VkCommandBufferAllocateInfo,commandPool,16);
SIZE(VkCommandBufferBeginInfo,32);
SIZE(VkMemoryBarrier,24);
OFF(VkMemoryBarrier,srcAccessMask,16);
SIZE(VkSubmitInfo,72);
OFF(VkSubmitInfo,pCommandBuffers,48);
_Static_assert(VK_STRUCTURE_TYPE_MEMORY_BARRIER == 46, "barrier");
_Static_assert(VK_ACCESS_TRANSFER_WRITE_BIT == 0x1000, "transfer write");
_Static_assert(VK_ACCESS_HOST_READ_BIT == 0x2000, "host read");
_Static_assert(VK_PIPELINE_STAGE_TRANSFER_BIT == 0x1000, "transfer stage");
_Static_assert(VK_PIPELINE_STAGE_HOST_BIT == 0x4000, "host stage");
int main(void) {return 0;}
]=] VPP_VULKAN_ABI)
  unset(CMAKE_REQUIRED_LIBRARIES)
  if(NOT VPP_VULKAN_ABI)
    message(FATAL_ERROR "ABI Vulkan không khớp với nguồn ASM")
  endif()
  add_executable(vpp-graphics-vulkan "${ASM_DIR}/vulkan_x86_64.S")
  target_link_libraries(vpp-graphics-vulkan PRIVATE Vulkan::Vulkan)
  install(TARGETS vpp-graphics-vulkan RUNTIME DESTINATION bin)
  if(BUILD_TESTING)
    add_test(NAME graphics-vulkan-readback COMMAND "${Python3_EXECUTABLE}"
      "${CMAKE_CURRENT_SOURCE_DIR}/check_gpu.py" "$<TARGET_FILE:vpp-graphics-vulkan>" --vulkan)
    set_tests_properties(graphics-vulkan-readback PROPERTIES TIMEOUT 60)
  endif()
endif()
if(BUILD_TESTING AND VPP_GRAPHICS_SDL)
  add_test(NAME graphics-opengl-readback COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/check_gpu.py" "$<TARGET_FILE:vpp-graphics-sdl>" --opengl)
  set_tests_properties(graphics-opengl-readback PROPERTIES TIMEOUT 60)
endif()
