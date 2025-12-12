cmake_minimum_required(VERSION 3.16)

add_executable(test_queue test_queue.cpp)
target_link_libraries(test_queue PRIVATE tds_core)
add_test(NAME queue COMMAND test_queue)

add_executable(test_protocol test_protocol.cpp)
target_link_libraries(test_protocol PRIVATE tds_core)
add_test(NAME protocol COMMAND test_protocol)

add_executable(test_kv_store test_kv_store.cpp)
target_link_libraries(test_kv_store PRIVATE tds_core)
add_test(NAME kv_store COMMAND test_kv_store)
