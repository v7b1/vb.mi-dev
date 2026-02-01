cmake_minimum_required(VERSION 3.19)


include(${CMAKE_CURRENT_SOURCE_DIR}/../../max-sdk-base/script/max-pretarget.cmake)

# paths to our sources
set(MUTABLE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../../mutableSources64")
set(STMLIB_PATH ${MUTABLE_PATH}/stmlib)
set(MI_PATH ${MUTABLE_PATH}/rings)

if(MSVC)
add_definitions(-D_USE_MATH_DEFINES) # defines M_PI with MSVC
endif()

set(STMLIB_SOURCES 
	${STMLIB_PATH}/stmlib.h
	${STMLIB_PATH}/utils/random.cc
	${STMLIB_PATH}/utils/random.h
	${STMLIB_PATH}/utils/dsp.h
	${STMLIB_PATH}/dsp/atan.cc
	${STMLIB_PATH}/dsp/atan.h
	${STMLIB_PATH}/dsp/units.cc
	${STMLIB_PATH}/dsp/units.h

)

set(MI_SOURCES
	${MI_PATH}/dsp/dsp.h
	${MI_PATH}/dsp/fm_voice.cc
	${MI_PATH}/dsp/fm_voice.h
	${MI_PATH}/dsp/follower.h
	${MI_PATH}/dsp/fx/chorus.h
	${MI_PATH}/dsp/fx/ensemble.h
	${MI_PATH}/dsp/fx/fx_engine.h
	${MI_PATH}/dsp/fx/reverb.h
	${MI_PATH}/dsp/limiter.h
	${MI_PATH}/dsp/note_filter.h
	${MI_PATH}/dsp/onset_detector.h
	${MI_PATH}/dsp/part.cc
	${MI_PATH}/dsp/part.h
	${MI_PATH}/dsp/patch.h
	${MI_PATH}/dsp/performance_state.h
	${MI_PATH}/dsp/plucker.h
	${MI_PATH}/dsp/resonator.cc
	${MI_PATH}/dsp/resonator.h
	${MI_PATH}/dsp/string.cc
	${MI_PATH}/dsp/string.h
	${MI_PATH}/dsp/string_synth_envelope.h
	${MI_PATH}/dsp/string_synth_oscillator.h
	${MI_PATH}/dsp/string_synth_part.cc
	${MI_PATH}/dsp/string_synth_part.h
	${MI_PATH}/dsp/string_synth_voice.h
	${MI_PATH}/dsp/strummer.h
	${MI_PATH}/resources.cc
	${MI_PATH}/resources.h
)


set(BUILD_SOURCES 
	${PROJECT_NAME}.cpp
	read_inputs.cpp
    	read_inputs.h
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

if(APPLE)
target_link_libraries(${PROJECT_NAME} PUBLIC "-framework Accelerate")
endif()

include(${CMAKE_CURRENT_SOURCE_DIR}/../../max-sdk-base/script/max-posttarget.cmake)
