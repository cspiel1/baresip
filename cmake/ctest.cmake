#
# ctest.cmake
#
# Copyright (C) 2024 c.huber@commend.com
#

##############################################################################
#
# CTest
#
include(CTest)
enable_testing()

file(STRINGS ${CMAKE_CURRENT_SOURCE_DIR}/main.c test_list
  REGEX "TEST\\(test_"
)

foreach (tc IN LISTS test_list)
	string(STRIP ${tc} tc)
  string(REPLACE "TEST(" "" tc ${tc})
  string(REPLACE ")," "" tc ${tc})
  list(APPEND test_cases ${tc})
endforeach()

foreach(tc IN LISTS test_cases)
  add_test(NAME "${tc}_main" COMMAND ./test/selftest -r main ${tc} WORKING_DIRECTORY ./../)
endforeach()

foreach(tc IN LISTS test_cases)
add_test(NAME "${tc}_thread" COMMAND ./test/selftest -r thread ${tc} WORKING_DIRECTORY ./../)
endforeach()
