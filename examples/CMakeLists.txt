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
