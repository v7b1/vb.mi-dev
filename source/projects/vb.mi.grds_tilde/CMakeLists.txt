cmake_minimum_required(VERSION 3.19)


include(${CMAKE_CURRENT_SOURCE_DIR}/../../max-sdk-base/script/max-pretarget.cmake)

# paths to our sources
set(MUTABLE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../../mutableSources32")
set(AVRLIB_PATH ${MUTABLE_PATH}/avrlib)
set(MI_PATH ${MUTABLE_PATH}/grids)


set(AVRLIB_SOURCES
    ${AVRLIB_PATH}/base.h
	${AVRLIB_PATH}/random.cc
    ${AVRLIB_PATH}/random.h
    ${AVRLIB_PATH}/op.h
)

set(MI_SOURCES
    ${MI_PATH}/clock.cc
	${MI_PATH}/clock.h
	${MI_PATH}/pattern_generator.cc
	${MI_PATH}/pattern_generator.h
	${MI_PATH}/resources.cc	
	${MI_PATH}/resources.h
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
	${AVRLIB_SOURCES}
	${MI_SOURCES}
	${BUILD_SOURCES}
)

# add preprocessor macro TEST
target_compile_definitions(${PROJECT_NAME} PUBLIC TEST)

# create groups in our project
source_group(TREE ${MUTABLE_PATH} FILES ${MI_SOURCES} ${AVRLIB_SOURCES})


include(${CMAKE_CURRENT_SOURCE_DIR}/../../max-sdk-base/script/max-posttarget.cmake)
