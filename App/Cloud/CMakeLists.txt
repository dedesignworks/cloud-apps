add_library(cloud Source/Cloud.c)

target_include_directories(cloud
    PUBLIC
        Include
        ${AZURE_SDK_INCLUDE_DIRS}
        ${DEV_AUTH_MODULES_CLIENT_INC_FOLDER}
        ${SHARED_UTIL_INC_FOLDER}
)

target_link_libraries(cloud
    PRIVATE
        iothub_client
        prov_device_client
        prov_mqtt_transport
        aziotsharedutil
)