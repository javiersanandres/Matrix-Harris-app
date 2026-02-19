# Create the library for node
add_library(node)

target_sources(node
    PRIVATE
         ${CMAKE_CURRENT_SOURCE_DIR}/Node.cpp
    PUBLIC
         ${CMAKE_CURRENT_SOURCE_DIR}/../include/Node.h
)

target_include_directories(node
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/../include
)

target_compile_features(node PUBLIC cxx_std_20)


# Create the library for diagram
add_library(diagram)

target_sources(diagram
    PRIVATE
         ${CMAKE_CURRENT_SOURCE_DIR}/Diagram.cpp
    PUBLIC
         ${CMAKE_CURRENT_SOURCE_DIR}/../include/Diagram.h
)

target_include_directories(diagram
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/../include
)

target_link_libraries(diagram
    PUBLIC node
)

target_compile_features(diagram PUBLIC cxx_std_20)