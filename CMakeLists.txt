cmake_minimum_required(VERSION 3.16)
project(AizoGuard LANGUAGES CXX)
add_executable(AizoGuard WIN32 src/main.cpp)
target_compile_features(AizoGuard PRIVATE cxx_std_17)
target_link_libraries(AizoGuard PRIVATE gdiplus gdi32 user32 comdlg32 comctl32 shlwapi winmm ole32 mfplat mfuuid)
if(MINGW)
  target_link_options(AizoGuard PRIVATE -municode -static -static-libgcc -static-libstdc++)
endif()
if(MSVC)
  target_compile_options(AizoGuard PRIVATE /utf-8)
endif()



