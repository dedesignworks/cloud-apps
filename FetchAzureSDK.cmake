Set(FETCHCONTENT_QUIET FALSE)

include(FetchContent)

FetchContent_Declare(azure_iot_sdk_c
                     GIT_REPOSITORY https://github.com/Azure/azure-iot-sdk-c.git
                     GIT_TAG        97fef570416467598100b782ef27ceadad9ca796
)
FetchContent_GetProperties(azure_iot_sdk_c)
if(NOT azure_iot_sdk_c_POPULATED)
    FetchContent_Populate(azure_iot_sdk_c)
endif()