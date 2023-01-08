cmake_minimum_required (VERSION 3.10)

add_compile_options (-std=gnu11 -march=native)

add_library (word1e guess.c threadpool.c)
target_include_directories (word1e PUBLIC include)

set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)
target_link_libraries(word1e PRIVATE Threads::Threads)
