#
# CMakeLists.txt for openwsman/bindings/ruby
#

enable_testing()

add_subdirectory(tests)

# prefer vendor dirs
IF(RUBY_HAS_VENDOR_RUBY)
  SET(RUBY_WSMAN_LIB_DIR ${RUBY_VENDORLIB_DIR})
  SET(RUBY_WSMAN_ARCH_DIR ${RUBY_VENDORARCH_DIR})
ELSE(RUBY_HAS_VENDOR_RUBY)
  SET(RUBY_WSMAN_LIB_DIR ${RUBY_SITELIB_DIR})
  SET(RUBY_WSMAN_ARCH_DIR ${RUBY_SITEARCH_DIR})
ENDIF(RUBY_HAS_VENDOR_RUBY)

MESSAGE(STATUS "Ruby executable: ${RUBY_EXECUTABLE}")
MESSAGE(STATUS "Ruby dir: ${RUBY_WSMAN_LIB_DIR}")
MESSAGE(STATUS "Ruby arch dir: ${RUBY_WSMAN_ARCH_DIR}")
MESSAGE(STATUS "Ruby include path: ${RUBY_INCLUDE_PATH}")

SET( SWIG_OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/openwsman_wrap.c" )
SET( SWIG_INPUT "${CMAKE_CURRENT_SOURCE_DIR}/../openwsman.i" )

ADD_CUSTOM_COMMAND (
   OUTPUT  ${SWIG_OUTPUT}
   COMMAND ${CMAKE_COMMAND} -E echo_append "Creating wrapper code for Ruby..."
   COMMAND ${SWIG_EXECUTABLE} -ruby -features autodoc -autorename -I${CMAKE_SOURCE_DIR}/bindings -I${CMAKE_SOURCE_DIR}/include -o ${SWIG_OUTPUT} ${SWIG_INPUT}
   COMMAND ${CMAKE_COMMAND} -E echo "Done."
   WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
   DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/../*.i
   DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/*.c
)

SET( CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall" )

SET(rbwsman_SRCS ${SWIG_OUTPUT} ${CMAKE_CURRENT_SOURCE_DIR}/../openwsman.c )
ADD_LIBRARY( rbwsman SHARED ${rbwsman_SRCS} )
# prevent 'lib...'
SET_TARGET_PROPERTIES( rbwsman PROPERTIES PREFIX "" )
SET_TARGET_PROPERTIES( rbwsman PROPERTIES OUTPUT_NAME "_openwsman" )

INCLUDE_DIRECTORIES( ${RUBY_INCLUDE_PATH} )
INCLUDE_DIRECTORIES( ${CMAKE_CURRENT_BINARY_DIR} )
INCLUDE_DIRECTORIES( ${CMAKE_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/bindings ${CMAKE_SOURCE_DIR}/include )

TARGET_LINK_LIBRARIES( rbwsman wsman )
TARGET_LINK_LIBRARIES( rbwsman wsman_client )
TARGET_LINK_LIBRARIES( rbwsman ${LIBXML2_LIBRARIES} )

# copy .so file to ARCH_DIR
INSTALL(TARGETS rbwsman LIBRARY DESTINATION ${RUBY_WSMAN_ARCH_DIR})

# copy openwsman/*.rb to LIB_DIR/openwsman/*.rb
INSTALL(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/openwsman DESTINATION ${RUBY_WSMAN_LIB_DIR}
        PATTERN ".rb"
	PATTERN ".svn" EXCLUDE
	PATTERN "Makefile.am" EXCLUDE)

INSTALL(FILES ${CMAKE_CURRENT_SOURCE_DIR}/openwsman.rb DESTINATION ${RUBY_WSMAN_LIB_DIR})

# rdoc
SET(rdoc_dir "${CMAKE_CURRENT_BINARY_DIR}/html")
ADD_CUSTOM_COMMAND (
   OUTPUT ${rdoc_dir}
   COMMAND ${CMAKE_COMMAND} -E echo_append "Creating rdoc documentation ..."
   COMMAND rm -rf ${rdoc_dir}
   COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/rdoc -o ${rdoc_dir} openwsman.i *.i
   COMMAND ${CMAKE_COMMAND} -E echo "Done."
   WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/bindings
   DEPENDS ${CMAKE_SOURCE_DIR}/bindings/*.i
)
ADD_CUSTOM_TARGET(ruby_rdoc ALL DEPENDS "${rdoc_dir}")
ADD_DEPENDENCIES(ruby_rdoc rbwsman)

#
# Leave this to %doc in the .spec file
#INSTALL(DIRECTORY "${rdoc_dir}" DESTINATION ${DOC_INSTALL_DIR})
