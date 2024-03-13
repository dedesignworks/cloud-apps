# Set Azure IoT SDK C settings
set(use_mqtt ON CACHE  BOOL "Set mqtt on" FORCE )
set(skip_samples ON CACHE  BOOL "Set slip_samples on" FORCE )
set(build_service_client OFF CACHE  BOOL "Set build_service_client off" FORCE )
set(build_provisioning_service_client OFF CACHE  BOOL "Set build_provisioning_service_client off" FORCE )
set(BUILD_TESTING OFF CACHE  BOOL "Set BUILD_TESTING off" FORCE )

add_subdirectory(${azure_iot_sdk_c_SOURCE_DIR} ${azure_iot_sdk_c_BINARY_DIR} EXCLUDE_FROM_ALL)
compileAsC99()

add_subdirectory(Cloud)
add_subdirectory(cloud-send)
add_subdirectory(cloud-provision)