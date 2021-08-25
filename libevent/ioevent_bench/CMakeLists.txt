set(REQUIRED_SOURCES)

set(TEST_LIBRARIES event)

# Build tests
add_executable(event_ioevent_bench
    ioevent_bench.c
    ${REQUIRED_SOURCES}
)
target_link_libraries(event_ioevent_bench ${TEST_LIBRARIES})
