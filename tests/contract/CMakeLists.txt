# Contract tests: verify API contracts are satisfied

add_executable(test_api_contracts test_api_contracts.c)
target_link_libraries(test_api_contracts PRIVATE engine_core)

add_test(NAME contract/api_contracts COMMAND test_api_contracts)
