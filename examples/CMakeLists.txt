add_executable(example1
    example1.cpp
 )

target_link_libraries(example1 PRIVATE newui)

add_executable(delegates1
    delegates1.cpp
)

target_link_libraries(delegates1 PRIVATE newui)

add_executable(fonts1
    fonts1.cpp)

target_link_libraries(fonts1 PRIVATE newui)

add_executable(properties1
    properties1.cpp
)

target_link_libraries(properties1 PRIVATE newui)

add_executable(idletasks1
    idletasks1.cpp
)

target_link_libraries(idletasks1 PRIVATE newui)

add_executable(animation1
    animation1.cpp
)

target_link_libraries(animation1 PRIVATE newui)

add_executable(layout1
    layout1.cpp
)

target_link_libraries(layout1 PRIVATE newui)
