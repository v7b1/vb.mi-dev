cmake_minimum_required(VERSION 3.0)


include(${CMAKE_CURRENT_SOURCE_DIR}/../../max-api/script/max-pretarget.cmake)

set(MUTABLE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../../mutableSources64")
set(STMLIB_PATH ${MUTABLE_PATH}/stmlib)
set(MI_PATH "${CMAKE_CURRENT_SOURCE_DIR}/mi")


set(MI_SOURCES
    ${MI_PATH}/omi/dsp/dsp.h
    ${MI_PATH}/omi/dsp/part.cc
    ${MI_PATH}/omi/dsp/part.h
    ${MI_PATH}/omi/dsp/patch.h
    ${MI_PATH}/omi/dsp/ominous_voice.cc
    ${MI_PATH}/omi/dsp/ominous_voice.h
    ${MI_PATH}/omi/dsp/multistage_envelope.cc
    ${MI_PATH}/omi/dsp/multistage_envelope.h
    ${MI_PATH}/omi/resources.cc
    ${MI_PATH}/omi/resources.h
)


add_library( 
	${PROJECT_NAME} 
	MODULE
	${PROJECT_NAME}.cpp
	${MI_SOURCES}
)

include_directories(
    "${C74_INCLUDES}"
    ${MUTABLE_PATH}
    ${MI_PATH}
)


# create groups in our project
source_group(TREE ${MI_PATH} FILES ${MI_SOURCES})


target_link_libraries(${PROJECT_NAME} PUBLIC "-framework Accelerate")


include(${CMAKE_CURRENT_SOURCE_DIR}/../../max-api/script/max-posttarget.cmake)
