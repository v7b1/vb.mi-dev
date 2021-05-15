cmake_minimum_required(VERSION 3.0)


include(${CMAKE_CURRENT_SOURCE_DIR}/../../max-api/script/max-pretarget.cmake)

# paths to our sources
set(MUTABLE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../../mutableSources64")
set(MI_PATH ${MUTABLE_PATH}/elements)
set(STMLIB_PATH ${MUTABLE_PATH}/stmlib)


set(STMLIB_SOURCES 
	${STMLIB_PATH}/stmlib.h
	${STMLIB_PATH}/utils/random.cc
	${STMLIB_PATH}/utils/random.h
	${STMLIB_PATH}/dsp/atan.cc
	${STMLIB_PATH}/dsp/atan.h
	${STMLIB_PATH}/dsp/units.cc
	${STMLIB_PATH}/dsp/units.h

)

set(MI_SOURCES
	${MI_PATH}/dsp/dsp.h
	${MI_PATH}/dsp/fx/diffuser.h
	${MI_PATH}/dsp/fx/fx_engine.h
	${MI_PATH}/dsp/fx/reverb.h
	${MI_PATH}/resources.cc
	${MI_PATH}/resources.h

)

set(BUILD_SOURCES 
	${PROJECT_NAME}.cpp
	dsp.h
	filter.h
	resonator.cc
	resonator.h
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

# add preprocessor macro TEST to avoid asm functions
target_compile_definitions(${PROJECT_NAME} PUBLIC TEST)

# create groups in our project
source_group(TREE ${MUTABLE_PATH} FILES ${STMLIB_SOURCES} ${MI_SOURCES})


target_link_libraries(${PROJECT_NAME} PUBLIC "-framework Accelerate")

include(${CMAKE_CURRENT_SOURCE_DIR}/../../max-api/script/max-posttarget.cmake)
