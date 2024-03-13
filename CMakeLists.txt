cmake_minimum_required(VERSION 3.16)

project(cloud-apps LANGUAGES C)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}")

include(FetchAzureSDK)

add_subdirectory(App)
