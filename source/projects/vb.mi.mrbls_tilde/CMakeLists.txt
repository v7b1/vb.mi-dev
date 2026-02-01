cmake_minimum_required(VERSION 3.19)


include(${CMAKE_CURRENT_SOURCE_DIR}/../../max-sdk-base/script/max-pretarget.cmake)

# paths to our sources
set(MUTABLE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../../mutableSources32")
set(STMLIB_PATH ${MUTABLE_PATH}/stmlib)
set(MARBLES_PATH ${MUTABLE_PATH}/marbles)

set(STMLIB_SOURCES 
	${STMLIB_PATH}/stmlib.h
	${STMLIB_PATH}/dsp/units.cc
	${STMLIB_PATH}/dsp/hysteresis_quantizer.h
	${STMLIB_PATH}/dsp/units.h
	${STMLIB_PATH}/dsp/dsp.h
	${STMLIB_PATH}/utils/gate_flags.h
)

set(MARBLES_SOURCES 
	${MARBLES_PATH}/note_filter.h
	${MARBLES_PATH}/resources.cc
	${MARBLES_PATH}/resources.h
	${MARBLES_PATH}/scale_recorder.h
	${MARBLES_PATH}/settings.cc
	${MARBLES_PATH}/settings.h
	${MARBLES_PATH}/cv_reader_channel.h
	${MARBLES_PATH}/ramp/ramp_extractor.cc
	${MARBLES_PATH}/ramp/ramp_divider.h
	${MARBLES_PATH}/ramp/ramp_extractor.h
	${MARBLES_PATH}/ramp/ramp_generator.h
	${MARBLES_PATH}/ramp/ramp.h
	${MARBLES_PATH}/ramp/slave_ramp.h
	${MARBLES_PATH}/random/lag_processor.cc
	${MARBLES_PATH}/random/discrete_distribution_quantizer.cc
	${MARBLES_PATH}/random/discrete_distribution_quantizer.h
	${MARBLES_PATH}/random/distributions.h
	${MARBLES_PATH}/random/lag_processor.cc
	${MARBLES_PATH}/random/lag_processor.h
	${MARBLES_PATH}/random/output_channel.cc
	${MARBLES_PATH}/random/output_channel.h
	${MARBLES_PATH}/random/quantizer.cc
	${MARBLES_PATH}/random/quantizer.h
	${MARBLES_PATH}/random/random_generator.h
	${MARBLES_PATH}/random/random_sequence.h
	${MARBLES_PATH}/random/random_stream.h
	${MARBLES_PATH}/random/t_generator.cc
	${MARBLES_PATH}/random/t_generator.h
	${MARBLES_PATH}/random/x_y_generator.cc
	${MARBLES_PATH}/random/x_y_generator.h
)



set(BUILD_SOURCES 
	${PROJECT_NAME}.cpp
    	read_inputs.cpp
    	read_inputs.hpp
	dsp.h
)


include_directories( 
	"${C74_INCLUDES}"
    ${MUTABLE_PATH}
)



add_library( 
	${PROJECT_NAME} 
	MODULE
	${STMLIB_SOURCES}
	${MARBLES_SOURCES}
	${BUILD_SOURCES}
)

# add preprocessor macro TEST to avoid asm functions
target_compile_definitions(${PROJECT_NAME} PUBLIC TEST)

# create groups in our project
source_group(TREE ${MUTABLE_PATH} FILES ${STMLIB_SOURCES} ${MARBLES_SOURCES})

if(APPLE)
target_link_libraries(${PROJECT_NAME} PUBLIC "-framework Accelerate")
endif()

include(${CMAKE_CURRENT_SOURCE_DIR}/../../max-sdk-base/script/max-posttarget.cmake)
