file(GLOB TESTS ${CMAKE_CURRENT_LIST_DIR}/../../test/*.cpp)
# WHOLE_ARCHIVE: each test file is reached only through its static registrations,
# which the linker would otherwise drop.
idf_component_register(SRCS app.cpp ${TESTS} INCLUDE_DIRS ../../test REQUIRES prim
                       PRIV_REQUIRES esp_hw_support WHOLE_ARCHIVE)
# Two arenas at once must fit in the S3's internal RAM.
target_compile_definitions(${COMPONENT_LIB} PRIVATE PRIM_TEST_HEAP=98304u)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra -Werror)
