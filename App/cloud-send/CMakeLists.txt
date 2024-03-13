set(EXE_NAME cloud-send)

add_executable(${EXE_NAME}
    Source/main.c
    Source/File.c
)

target_include_directories(${EXE_NAME}
    SYSTEM
    PUBLIC
        ${CMAKE_CURRENT_LIST_DIR}/Include
        ${AZURE_SDK_INCLUDE_DIRS}
)

target_link_libraries(${EXE_NAME}
    PRIVATE
        json-c
        iothub_client
        cloud
)

install(
    TARGETS ${EXE_NAME}
    RUNTIME DESTINATION bin
    LIBRARY DESTINATION lib
)