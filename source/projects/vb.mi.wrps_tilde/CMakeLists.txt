cmake_minimum_required(VERSION 3.0)


include(${CMAKE_CURRENT_SOURCE_DIR}/../../max-sdk-base/script/max-pretarget.cmake)

# paths to our sources
set(MUTABLE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../../mutableSources32")
set(STMLIB_PATH ${MUTABLE_PATH}/stmlib)
set(MI_PATH ${MUTABLE_PATH}/warps)

set(STMLIB_SOURCES 
	${STMLIB_PATH}/stmlib.h
	${STMLIB_PATH}/utils/random.cc
	${STMLIB_PATH}/utils/random.h
	${STMLIB_PATH}/dsp/units.cc
	${STMLIB_PATH}/dsp/units.h
)

set(MI_SOURCES
	${MI_PATH}/dsp/filter_bank.cc
	${MI_PATH}/dsp/filter_bank.h
	${MI_PATH}/dsp/limiter.h
	${MI_PATH}/dsp/modulator.cc
	${MI_PATH}/dsp/modulator.h
	${MI_PATH}/dsp/oscillator.cc
	${MI_PATH}/dsp/oscillator.h
	${MI_PATH}/dsp/parameters.h
	${MI_PATH}/dsp/quadrature_oscillator.h
	${MI_PATH}/dsp/quadrature_transform.h
	${MI_PATH}/dsp/sample_rate_conversion_filters.h
	${MI_PATH}/dsp/sample_rate_converter.h
	${MI_PATH}/dsp/vocoder.cc
	${MI_PATH}/dsp/vocoder.h
	${MI_PATH}/resources.cc
	${MI_PATH}/resources.h
)

set(BUILD_SOURCES 
	${PROJECT_NAME}.cpp
	read_inputs.cpp	
	read_inputs.hpp
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


# add preprocessor macro to avoid asm functions
target_compile_definitions(${PROJECT_NAME} PUBLIC TEST)


# create groups in our project
source_group(TREE ${MUTABLE_PATH} FILES ${STMLIB_SOURCES} ${MI_SOURCES})


include(${CMAKE_CURRENT_SOURCE_DIR}/../../max-sdk-base/script/max-posttarget.cmake)
