# Set up project
cmake_minimum_required(VERSION 3.6)
project(ModIndexer)
set(CMAKE_CXX_STANDARD 17)

message("--- Setting up ModIndexer")


### Libraries

find_package(CURL REQUIRED)
set(MYHTML_LIBRARY "" CACHE FILEPATH "Path to libmyhtml.so")
set(MYHTML_INCLUDE_DIR "" CACHE PATH "Path to include")


### Compiler options

if (NOT MSVC)
	# -O3 is default for release
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall")

	# Debugging symbols
	set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_C_FLAGS} -g -O0")

	set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -Wl,--no-undefined")
endif()


### Source files

add_subdirectory("${PROJECT_SOURCE_DIR}/lib")

set(SRC_FILES
	${LIB_FILES}
	main.cpp
)

include_directories(
	${CURL_INCLUDE_DIRS}
	"${MYHTML_INCLUDE_DIR}"
)


### Library creation

add_executable(${PROJECT_NAME} ${SRC_FILES})
target_link_libraries(
	${PROJECT_NAME}
	${CURL_LIBRARIES}
	${CMAKE_DL_LIBS}
	${MYHTML_LIBRARY}
)
