cmake_minimum_required(VERSION 3.0)


include(${CMAKE_CURRENT_SOURCE_DIR}/../../max-api/script/max-pretarget.cmake)

# paths to our sources
set(MUTABLE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../../parasites")
set(STMLIB_PATH ${MUTABLE_PATH}/stmlib)
set(MI_PATH ${MUTABLE_PATH}/tides)


set(STMLIB_SOURCES 
	${STMLIB_PATH}/stmlib.h
	${STMLIB_PATH}/dsp/dsp.h
    ${STMLIB_PATH}/algorithms/pattern_predictor.h
    ${STMLIB_PATH}/utils/dsp.h
    ${STMLIB_PATH}/utils/ring_buffer.h
    ${STMLIB_PATH}/utils/random.h
    ${STMLIB_PATH}/utils/random.cc
)

set(MI_SOURCES
	${MI_PATH}/resources.cc
	${MI_PATH}/resources.h
	${MI_PATH}/generator.cc
	${MI_PATH}/generator.h
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
	${STMLIB_SOURCES}
	${MI_SOURCES}
	${BUILD_SOURCES}
)

# add preprocessor macro TEST
target_compile_definitions(${PROJECT_NAME} PUBLIC TEST)

# create groups in our project
source_group(TREE ${MUTABLE_PATH} FILES ${MI_SOURCES} ${STMLIB_SOURCES})


include(${CMAKE_CURRENT_SOURCE_DIR}/../../max-api/script/max-posttarget.cmake)
