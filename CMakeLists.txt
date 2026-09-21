cmake_minimum_required(VERSION 3.20)
project(NRFusion VERSION 0.5.4 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(cmake/NRFusionCore.cmake)
include(cmake/NRFusionTests.cmake)
include(cmake/NRFusionTools.cmake)
if (WIN32)
    include(cmake/NRFusionWindows.cmake)
endif()
