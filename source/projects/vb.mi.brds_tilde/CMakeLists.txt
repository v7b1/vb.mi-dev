cmake_minimum_required(VERSION 3.0)


include(${CMAKE_CURRENT_SOURCE_DIR}/../../max-sdk-base/script/max-pretarget.cmake)

# paths to our sources
set(MUTABLE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../../mutableSources32")
set(STMLIB_PATH ${MUTABLE_PATH}/stmlib)
set(MI_PATH ${MUTABLE_PATH}/braids)
set(LIBSR_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../../libs/libsamplerate")

#set(LIBSR_PATH "${CMAKE_CURRENT_SOURCE_DIR}/libsamplerate/build/src")
#set(LIBSRI_PATH "${CMAKE_CURRENT_SOURCE_DIR}/libsamplerate/include")

set(STMLIB_SOURCES 
	${STMLIB_PATH}/stmlib.h
	${STMLIB_PATH}/utils/random.cc
	${STMLIB_PATH}/utils/random.h
	${STMLIB_PATH}/utils/dsp.h
	${STMLIB_PATH}/dsp/atan.cc
	${STMLIB_PATH}/dsp/atan.h
	${STMLIB_PATH}/dsp/units.cc
	${STMLIB_PATH}/dsp/units.h
	${STMLIB_PATH}/dsp/dsp.h
)

set(MI_SOURCES
	${MI_PATH}/analog_oscillator.cc
	${MI_PATH}/analog_oscillator.h
	${MI_PATH}/digital_oscillator.cc
	${MI_PATH}/digital_oscillator.h
	${MI_PATH}/envelope.h
	${MI_PATH}/excitation.h
	${MI_PATH}/macro_oscillator.cc
	${MI_PATH}/macro_oscillator.h
	${MI_PATH}/parameter_interpolation.h
	${MI_PATH}/quantizer.cc
	${MI_PATH}/quantizer.h
	${MI_PATH}/quantizer_scales.h
	${MI_PATH}/resources.cc
	${MI_PATH}/resources.h
	${MI_PATH}/signature_waveshaper.h
	${MI_PATH}/vco_jitter_source.h
)

set(BUILD_SOURCES 
	${PROJECT_NAME}.cpp
	# ${LIB_PATH}/samplerate.h
)


include_directories( 
	"${C74_INCLUDES}"
    ${MUTABLE_PATH}
	${LIBSR_PATH}/include
)



add_library( 
	${PROJECT_NAME} 
	MODULE
	${STMLIB_SOURCES}
	${MI_SOURCES}
	${BUILD_SOURCES}
)


# add preprocessor macro TEST to avoid asm functions
target_compile_definitions(${PROJECT_NAME} PUBLIC TEST)

# create groups in our project
source_group(TREE ${MUTABLE_PATH} FILES ${MI_SOURCES} ${STMLIB_SOURCES})

if(APPLE)
	target_link_libraries(${PROJECT_NAME} PUBLIC "-framework Accelerate")
	target_link_libraries(${PROJECT_NAME} PUBLIC ${LIBSR_PATH}/build/src/libsamplerate.a)
else()
	target_link_libraries(${PROJECT_NAME} PUBLIC ${LIBSR_PATH}/build/src/Release/samplerate.lib)
endif()

        
include(${CMAKE_CURRENT_SOURCE_DIR}/../../max-sdk-base/script/max-posttarget.cmake)
