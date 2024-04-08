
set(SKIP_RPPLS FALSE)
if (APPLE)
    if (NOT CMAKE_OSX_ARCHITECTURES)
        set(CMAKE_OSX_ARCHITECTURES x86_64)
    endif()
    if (CMAKE_OSX_ARCHITECTURES MATCHES "arm64")
		#set(SKIP_RPPLS TRUE)
		#message("skipping rppls")
	endif()
endif()

if(NOT SKIP_RPPLS)

include(${CMAKE_CURRENT_SOURCE_DIR}/../../max-sdk-base/script/max-pretarget.cmake)

# paths to our sources
set(RPPLS_PATH "${CMAKE_CURRENT_SOURCE_DIR}/vcvrack/Ripples")
set(VCV_INCLUDE "${CMAKE_CURRENT_SOURCE_DIR}/vcvrack/include")

set(VCV_SOURCES 
	${RPPLS_PATH}/aafilter.hpp
	${RPPLS_PATH}/ripples.hpp
	${RPPLS_PATH}/sos.hpp
)



add_library( 
	${PROJECT_NAME} 
	MODULE
	${PROJECT_NAME}.cpp
	${VCV_SOURCES}
)

include_directories(
    "${C74_INCLUDES}"
    ${RPPLS_PATH}
	${VCV_INCLUDE}
    if (CMAKE_OSX_ARCHITECTURES MATCHES "arm64")
        set(VCV_INCLUDE "${CMAKE_CURRENT_SOURCE_DIR}/vcvrack/dep")
    endif()
)


# create groups in our project
source_group(TREE ${RPPLS_PATH} FILES ${VCV_SOURCES})


#target_link_libraries(${PROJECT_NAME} PUBLIC "-framework Accelerate")


include(${CMAKE_CURRENT_SOURCE_DIR}/../../max-sdk-base/script/max-posttarget.cmake)

endif()
