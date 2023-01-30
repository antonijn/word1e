cmake_minimum_required (VERSION 3.10)
project (wordle-solve LANGUAGES C)

add_compile_options (-std=gnu11 -D_GNU_SOURCE -march=native)

add_subdirectory(libword1e)

add_executable(mkwx mkwx.c)
add_executable(wbot wbot.c)
add_executable(wordsmith json.c wordsmith.c)

set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)
target_link_libraries(mkwx PRIVATE word1e Threads::Threads)
target_link_libraries(wbot PRIVATE word1e Threads::Threads)
target_link_libraries(wordsmith PRIVATE word1e Threads::Threads)
