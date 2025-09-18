add_executable(event_server
    main.cpp
    configmgr.cpp
    server.cpp
    event_loop.cpp
    io_thread.cpp
    session.cpp
)

target_link_libraries(event_server PRIVATE Threads::Threads)