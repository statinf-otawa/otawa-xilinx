CMAKE_MINIMUM_REQUIRED(VERSION 2.6)

# configuration
set(SCRIPT	"xilinx")
set(FILES	
    "Xilinx/memory_r5f.xml" 
    "Xilinx/pipeline_r5f.xml" 
    "Xilinx/cache_r5f.xml" 
    "Xilinx/bht_r5f.xml"
    
    "Xilinx/memory_a9.xml"
    "Xilinx/pipeline_a9.xml"
    "Xilinx/cache_a9.xml"
    "Xilinx/bht_a9.xml"
)

set(SOURCES
	hook.cpp
    preFetchAnalysis.cpp
    arm_properties.h
    armCortexR5_A9_operand.h
    
	xilinxR5BBTime.cpp
    armCortexR5_time.h
	R5CycleTiming.h

    xilinxA9BBTime.cpp
    armCortexA9_time.h
    A9CycleTiming.h
)
set(ARM_HOME	"${CMAKE_SOURCE_DIR}/../armv7t")
set(GLISS_HOME 	"${CMAKE_SOURCE_DIR}/../gliss2")

# looking for required resources
find_file(IRG arm.irg PATHS "${ARM_HOME}")
find_program(GLISS_ATTR "gliss-attr" PATHS "${GLISS_HOME}/gep")


# script
project(${SCRIPT})


# look for OTAWA
if(NOT OTAWA_CONFIG)
	find_program(OTAWA_CONFIG otawa-config DOC "path to otawa-config" PATHS "${GLISS_HOME}/../../../otawa-install/bin")
	if(NOT OTAWA_CONFIG)
		message(FATAL_ERROR "ERROR: otawa-config is required !")
	endif()
endif()
message(STATUS "otawa-config at ${OTAWA_CONFIG}")

if(PREFIX)
	set(OTAWA_PREFIX ${PREFIX})
else()
	execute_process(COMMAND "${OTAWA_CONFIG}" --prefix OUTPUT_VARIABLE OTAWA_PREFIX OUTPUT_STRIP_TRAILING_WHITESPACE)
endif()
set(CMAKE_INSTALL_PREFIX "${OTAWA_PREFIX}")
execute_process(COMMAND "${OTAWA_CONFIG}" --cflags OUTPUT_VARIABLE OTAWA_CFLAGS OUTPUT_STRIP_TRAILING_WHITESPACE)
execute_process(COMMAND "${OTAWA_CONFIG}" -p ${SCRIPT}.eld --libs --rpath -p ${SCRIPT}.eld  OUTPUT_VARIABLE OTAWA_LDFLAGS OUTPUT_STRIP_TRAILING_WHITESPACE)
execute_process(COMMAND "${OTAWA_CONFIG}" --plugdir  OUTPUT_VARIABLE PLUGDIR OUTPUT_STRIP_TRAILING_WHITESPACE)
execute_process(COMMAND "${OTAWA_CONFIG}" --scriptdir  OUTPUT_VARIABLE SCRIPTDIR OUTPUT_STRIP_TRAILING_WHITESPACE)
message(STATUS "CMAKE_INSTALL_PREFIX = ${CMAKE_INSTALL_PREFIX}")

add_custom_command(
	OUTPUT "armCortexR5_time.h" DEPENDS "otawa_R5Time.tpl" "otawa_R5Time.nmp" ${IRG}
	COMMAND ${GLISS_ATTR}
	ARGS ${IRG} -o armCortexR5_time.h -a cortexR5_time -t "otawa_R5Time.tpl" -d "SET_TIME(R5_time_unknown);" -e otawa_R5Time.nmp -p
	VERBATIM
)

add_custom_command(
	OUTPUT "armCortexR5_A9_operand.h" DEPENDS "otawa_operand.tpl" "otawa_operand.nmp" ${IRG}
	COMMAND ${GLISS_ATTR}
	ARGS ${IRG} -o armCortexR5_A9_operand.h -a arm7_op -t "otawa_operand.tpl" -d "SET_OP(1);" -e otawa_operand.nmp -p
	VERBATIM
)

add_custom_command(
	OUTPUT "armCortexA9_time.h" DEPENDS "otawa_A9Time.tpl" "otawa_A9Time.nmp" ${IRG}
	COMMAND ${GLISS_ATTR}
	ARGS ${IRG} -o armCortexA9_time.h -a cortexA9_time -t "otawa_A9Time.tpl" -d "SET_TIME(A9_time_unknown);" -e otawa_A9Time.nmp -p
	VERBATIM
)

# plugin definition
include_directories("${CMAKE_SOURCE_DIR}" "${ARM_HOME}/include")
add_library(${SCRIPT} MODULE ${SOURCES})
set_property(TARGET ${SCRIPT} PROPERTY PREFIX "")
set_property(TARGET ${SCRIPT} PROPERTY COMPILE_FLAGS ${OTAWA_CFLAGS})
target_link_libraries(${SCRIPT} "${OTAWA_LDFLAGS}")


# installation
install(TARGETS ${SCRIPT} LIBRARY DESTINATION "${PLUGDIR}/otawa")
#install(FILES "${SCRIPT}.eld" DESTINATION "${PLUGDIR}/otawa")
install(FILES "${SCRIPT}.eld" DESTINATION "${PLUGDIR}/otawa")
install(FILES "${SCRIPT}.osx" DESTINATION "${SCRIPTDIR}")
foreach(FILE ${FILES})
	install(FILES ${CMAKE_SOURCE_DIR}/${FILE} DESTINATION "${SCRIPTDIR}/Xilinx")
endforeach()
