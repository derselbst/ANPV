
ENABLE_TESTING()

add_custom_target(buildtests)

add_custom_target(check COMMAND ${CMAKE_CTEST_COMMAND} --verbose)
add_dependencies(check buildtests)


macro ( ADD_ANPV_TEST _test )
    ADD_EXECUTABLE(${_test} ${_test}.cpp )
    
    # only build this unit test when explicitly requested by "make check"
    set_target_properties(${_test} PROPERTIES EXCLUDE_FROM_ALL TRUE)
    
    TARGET_LINK_LIBRARIES(${_test} PRIVATE anpv-lib Qt6::Core Qt6::Widgets Qt6::Test)
    
    # add the test to ctest
    ADD_TEST(NAME ${_test} COMMAND ${_test})
    
    # append the current unit test to check-target as dependency
    add_dependencies(buildtests ${_test})

endmacro ( ADD_ANPV_TEST )


ADD_ANPV_TEST(ImageTest)
