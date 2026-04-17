add_executable(order_book_tests
    test_order_book.cpp
)

target_link_libraries(order_book_tests
    PRIVATE
        matching_engine_core
)

add_test(NAME order_book_tests COMMAND order_book_tests)

add_executable(matching_engine_tests
    test_matching_engine.cpp
)

target_link_libraries(matching_engine_tests
    PRIVATE
        matching_engine_core
)

add_test(NAME matching_engine_tests COMMAND matching_engine_tests)

add_executable(order_book_fuzz_tests
    test_order_book_fuzz.cpp
)

target_link_libraries(order_book_fuzz_tests
    PRIVATE
        matching_engine_core
)

add_test(NAME order_book_fuzz_tests COMMAND order_book_fuzz_tests)
