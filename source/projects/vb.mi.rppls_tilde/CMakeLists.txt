cmake_minimum_required(VERSION 3.0)

set(SKIP_RPPLS FALSE)
if (APPLE)
    if (NOT CMAKE_OSX_ARCHITECTURES)
        set(CMAKE_OSX_ARCHITECTURES x86_64)
    endif()
    if (CMAKE_OSX_ARCHITECTURES MATCHES "arm64")
		set(SKIP_RPPLS TRUE)
		message("skipping rppls")
	endif()
endif()

if(NOT SKIP_RPPLS)

include(${CMAKE_CURRENT_SOURCE_DIR}/../../max-sdk-base/script/max-pretarget.cmake)

# paths to our sources
set(MUTABLE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../../mutableSources32")
#set(STMLIB_PATH ${MUTABLE_PATH}/stmlib)
set(VCV_PATH "${CMAKE_CURRENT_SOURCE_DIR}/vcvrack")
set(VCV_INCLUDE "${CMAKE_CURRENT_SOURCE_DIR}/vcvrack/include")

set(VCV_SOURCES 
	${VCV_PATH}/Ripples/aafilter.hpp
	${VCV_PATH}/Ripples/ripples.hpp
	${VCV_PATH}/Ripples/sos.hpp
	${VCV_PATH}/src/random.cpp
)



set(BUILD_SOURCES 
	${PROJECT_NAME}.cpp
)


add_library( 
	${PROJECT_NAME} 
	MODULE
	${PROJECT_NAME}.cpp
	${VCV_SOURCES}
)

include_directories(
    "${C74_INCLUDES}"
    #${MUTABLE_PATH}
    ${VCV_PATH}
	${VCV_INCLUDE}
)


# create groups in our project
source_group(TREE ${VCV_PATH} FILES ${VCV_SOURCES})


#target_link_libraries(${PROJECT_NAME} PUBLIC "-framework Accelerate")


include(${CMAKE_CURRENT_SOURCE_DIR}/../../max-sdk-base/script/max-posttarget.cmake)

endif()