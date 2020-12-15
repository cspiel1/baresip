project(commod)

find_package(SYSTEMD)

list(APPEND MODULES_DETECTED ${PROJECT_NAME})
set(MODULES_DETECTED ${MODULES_DETECTED} PARENT_SCOPE)

set(SRCS commod.c)

if(STATIC)
    add_library(${PROJECT_NAME} OBJECT ${SRCS})
else()
    add_library(${PROJECT_NAME} MODULE ${SRCS})
endif()

target_include_directories(${PROJECT_NAME} PRIVATE ${SYSTEMD_INCLUDE_DIRS})
target_link_libraries(${PROJECT_NAME} PRIVATE ${SYSTEMD_LIBRARIES})
target_compile_options(${PROJECT_NAME} PRIVATE -Wno-unused-function)
