find_package(Boost REQUIRED COMPONENTS system thread)

set(TEST_LIBRARIES event Threads::Threads  ${Boost_SYSTEM_LIBRARY} ${Boost_THREAD_LIBRARY})

add_executable(asio_pp
    client.cpp
    server.cpp
)
target_link_libraries(asio_pp ${TEST_LIBRARIES})
target_precompile_headers(asio_pp PRIVATE pch.h)

# add_executable(benchmark_tcp_asio_server
#     asio.cpp
#     server.cpp
# )
# target_link_libraries(benchmark_tcp_asio_server ${TEST_LIBRARIES})
