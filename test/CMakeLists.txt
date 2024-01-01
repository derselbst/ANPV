
ENABLE_TESTING()

add_custom_target(buildtests)

add_custom_target(check COMMAND ${CMAKE_CTEST_COMMAND} -C $<CONFIG> --timeout 120 --verbose --output-on-failure)
add_dependencies(check buildtests)


macro ( ADD_ANPV_TEST _test )
    ADD_EXECUTABLE(${_test} ${_test}.cpp ${_test}.hpp )
    
    # only build this unit test when explicitly requested by "make check"
    set_target_properties(${_test} PROPERTIES EXCLUDE_FROM_ALL TRUE)
    
    TARGET_LINK_LIBRARIES(${_test} PRIVATE anpv-lib Qt6::Core Qt6::Widgets Qt6::Test)
    
    # add the test to ctest
    if(DEFINED ENV{AGENT_ID})
        ADD_TEST(NAME ${_test} COMMAND ${_test} "-platform" "offscreen")
    else()
        ADD_TEST(NAME ${_test} COMMAND ${_test})
    endif()

    # append the current unit test to check-target as dependency
    add_dependencies(buildtests ${_test})

endmacro ( ADD_ANPV_TEST )


ADD_ANPV_TEST(ImageTest)
ADD_ANPV_TEST(DecoderTest)
ADD_ANPV_TEST(ProgressWidgetTest)
ADD_ANPV_TEST(MoonPhaseTest)
