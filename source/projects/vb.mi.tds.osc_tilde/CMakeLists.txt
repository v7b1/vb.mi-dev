cmake_minimum_required(VERSION 3.19)


include(${CMAKE_CURRENT_SOURCE_DIR}/../../max-sdk-base/script/max-pretarget.cmake)

# paths to our sources
set(MUTABLE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../../mutableSources32")
set(STMLIB_PATH ${MUTABLE_PATH}/stmlib)
set(MI_PATH "${CMAKE_CURRENT_SOURCE_DIR}/tides2")



set(MI_SOURCES
	${MI_PATH}/resources.cc
	${MI_PATH}/resources.h
	${MI_PATH}/poly_slope_generator.cc
	${MI_PATH}/poly_slope_generator.h
)

set(BUILD_SOURCES 
	${PROJECT_NAME}.cpp
)


include_directories( 
	"${C74_INCLUDES}"
    ${MUTABLE_PATH}
)



add_library( 
	${PROJECT_NAME} 
	MODULE
	${MI_SOURCES}
	${BUILD_SOURCES}
)

# add preprocessor macro TEST
target_compile_definitions(${PROJECT_NAME} PUBLIC TEST)

# create groups in our project
source_group(TREE ${MI_PATH} FILES ${MI_SOURCES})


include(${CMAKE_CURRENT_SOURCE_DIR}/../../max-sdk-base/script/max-posttarget.cmake)
