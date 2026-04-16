cmake_minimum_required(VERSION 3.20)

project(matching_engine VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
	set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Build type" FORCE)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(ENABLE_WARNINGS "Enable strict compiler warnings" ON)
option(ENABLE_SANITIZERS "Enable Address/Undefined sanitizers for debug" OFF)
option(ENABLE_LTO "Enable link-time optimization" OFF)
option(ENABLE_GNU_LOW_LATENCY_FLAGS "Enable Linux/GCC low-latency tuning flags" ON)

add_library(matching_engine_core
	src/order_book.cpp
	src/matching_engine.cpp
)

target_include_directories(matching_engine_core
	PUBLIC
		${CMAKE_CURRENT_SOURCE_DIR}/include
)

add_executable(matching_engine
	src/main.cpp
)

target_link_libraries(matching_engine
	PRIVATE
		matching_engine_core
)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND NOT CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
	message(WARNING "Linux low-latency deployments typically use GCC. Configure with: CC=gcc CXX=g++ cmake -S . -B build")
endif()

if(ENABLE_WARNINGS)
	if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|AppleClang")
		target_compile_options(matching_engine_core PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow)
		target_compile_options(matching_engine PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow)
	elseif(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
		target_compile_options(matching_engine_core PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow)
		target_compile_options(matching_engine PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow)
	elseif(MSVC)
		target_compile_options(matching_engine_core PRIVATE /W4 /permissive-)
		target_compile_options(matching_engine PRIVATE /W4 /permissive-)
	endif()
endif()

if(ENABLE_SANITIZERS AND CMAKE_CXX_COMPILER_ID MATCHES "Clang|AppleClang|GNU")
	target_compile_options(matching_engine_core PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
	target_compile_options(matching_engine PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
	target_link_options(matching_engine_core PRIVATE -fsanitize=address,undefined)
	target_link_options(matching_engine PRIVATE -fsanitize=address,undefined)
endif()

if(ENABLE_GNU_LOW_LATENCY_FLAGS AND CMAKE_SYSTEM_NAME STREQUAL "Linux" AND CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
	target_compile_options(matching_engine_core PRIVATE
		$<$<CONFIG:Release>:-O3 -march=native -mtune=native -fno-plt>
		$<$<CONFIG:RelWithDebInfo>:-O3 -march=native -mtune=native -fno-plt>
	)
	target_compile_options(matching_engine PRIVATE
		$<$<CONFIG:Release>:-O3 -march=native -mtune=native -fno-plt>
		$<$<CONFIG:RelWithDebInfo>:-O3 -march=native -mtune=native -fno-plt>
	)
	target_link_options(matching_engine PRIVATE
		$<$<CONFIG:Release>:-Wl,--as-needed>
		$<$<CONFIG:RelWithDebInfo>:-Wl,--as-needed>
	)
endif()

if(ENABLE_LTO)
	include(CheckIPOSupported)
	check_ipo_supported(RESULT ipo_supported OUTPUT ipo_error)
	if(ipo_supported)
		set_property(TARGET matching_engine_core PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
		set_property(TARGET matching_engine PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
	else()
		message(WARNING "IPO/LTO not supported: ${ipo_error}")
	endif()
endif()

include(CTest)
if(BUILD_TESTING AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/CMakeLists.txt")
	add_subdirectory(tests)
endif()
