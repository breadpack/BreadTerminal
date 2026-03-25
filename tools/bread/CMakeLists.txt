add_executable(bread
    main.cpp
    cli_client.cpp
    arg_parser.cpp
    tmux_compat.cpp
    output_formatter.cpp
    hooks_installer.cpp
    local_commands.cpp
    osc_emitter.cpp
)

target_compile_features(bread PRIVATE cxx_std_20)
target_link_libraries(bread PRIVATE nlohmann_json::nlohmann_json)

install(TARGETS bread DESTINATION bin)
