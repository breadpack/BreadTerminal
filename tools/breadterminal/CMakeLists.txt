add_executable(breadterminal
    main.cpp
    cli_client.cpp
    arg_parser.cpp
    output_formatter.cpp
)

target_compile_features(breadterminal PRIVATE cxx_std_20)
target_link_libraries(breadterminal PRIVATE nlohmann_json::nlohmann_json)

install(TARGETS breadterminal DESTINATION bin)
