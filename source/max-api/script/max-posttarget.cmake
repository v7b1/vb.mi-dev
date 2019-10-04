# Copyright (c) 2016, Cycling '74
# Usage of this file and its contents is governed by the MIT License

if (${C74_CXX_STANDARD} EQUAL 98)
	if (APPLE)
		set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=gnu++98 -stdlib=libstdc++")
		set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -stdlib=libstdc++")
		set(CMAKE_MODULE_LINKER_FLAGS "${CMAKE_MODULE_LINKER_FLAGS} -stdlib=libstdc++")
	endif ()
else ()
	set_property(TARGET ${PROJECT_NAME} PROPERTY CXX_STANDARD 11)
	set_property(TARGET ${PROJECT_NAME} PROPERTY CXX_STANDARD_REQUIRED ON)
endif ()

if ("${PROJECT_NAME}" MATCHES ".*_tilde")
	string(REGEX REPLACE "_tilde" "~" EXTERN_OUTPUT_NAME "${PROJECT_NAME}")
else ()
    set(EXTERN_OUTPUT_NAME "${PROJECT_NAME}")
endif ()
set_target_properties(${PROJECT_NAME} PROPERTIES OUTPUT_NAME "${EXTERN_OUTPUT_NAME}")



### Output ###
if (APPLE)
	target_link_libraries(${PROJECT_NAME} "-framework JitterAPI")
	set_target_properties(${PROJECT_NAME} PROPERTIES LINK_FLAGS "-Wl,-F\"${C74_MAX_API_DIR}/lib/mac\"")
	
	set_property(TARGET ${PROJECT_NAME}
				 PROPERTY BUNDLE True)
	set_property(TARGET ${PROJECT_NAME}
				 PROPERTY BUNDLE_EXTENSION "mxo")	
	set_target_properties(${PROJECT_NAME} PROPERTIES XCODE_ATTRIBUTE_WRAPPER_EXTENSION "mxo")
	set_target_properties(${PROJECT_NAME} PROPERTIES MACOSX_BUNDLE_BUNDLE_VERSION "${GIT_VERSION_TAG}")
    set_target_properties(${PROJECT_NAME} PROPERTIES MACOSX_BUNDLE_INFO_PLIST ${CMAKE_CURRENT_LIST_DIR}/Info.plist.in)	
elseif (WIN32)
	target_link_libraries(${PROJECT_NAME} ${MaxAPI_LIB})
	target_link_libraries(${PROJECT_NAME} ${MaxAudio_LIB})
	target_link_libraries(${PROJECT_NAME} ${Jitter_LIB})

	if (CMAKE_SIZEOF_VOID_P EQUAL 8)
		set_target_properties(${PROJECT_NAME} PROPERTIES SUFFIX ".mxe64")
	else ()
		set_target_properties(${PROJECT_NAME} PROPERTIES SUFFIX ".mxe")
	endif ()

	# warning about constexpr not being const in c++14
	set_target_properties(${PROJECT_NAME} PROPERTIES COMPILE_FLAGS "/wd4814")

endif ()


### Post Build ###
#if (WIN32)
#	add_custom_command( 
#		TARGET ${PROJECT_NAME} 
#		POST_BUILD 
#		COMMAND rm "${CMAKE_LIBRARY_OUTPUT_DIRECTORY}/${EXTERN_OUTPUT_NAME}.ilk" 
#		COMMENT "ilk file cleanup" 
#	)
#endif ()
