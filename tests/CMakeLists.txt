add_executable(order_book_tests
    test_order_book.cpp
)

target_link_libraries(order_book_tests
    PRIVATE
        matching_engine_core
)

add_test(NAME order_book_tests COMMAND order_book_tests)
