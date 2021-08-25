set(REQUIRED_SOURCES)

set(TEST_LIBRARIES event Threads::Threads)

# Build tests

add_executable(event_pp
    client.c
    server.c
)
target_link_libraries(event_pp ${TEST_LIBRARIES})
