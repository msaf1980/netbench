string(REGEX MATCH "Clang" CMAKE_COMPILER_IS_CLANG "${CMAKE_C_COMPILER_ID}")
string(REGEX MATCH "GNU" CMAKE_COMPILER_IS_GNU "${CMAKE_C_COMPILER_ID}")
string(REGEX MATCH "IAR" CMAKE_COMPILER_IS_IAR "${CMAKE_C_COMPILER_ID}")
string(REGEX MATCH "MSVC" CMAKE_COMPILER_IS_MSVC "${CMAKE_C_COMPILER_ID}")

function(append_flag FLAGS_VAR FLAG_VAR)
	string(FIND "${${FLAGS_VAR}}" "${FLAG_VAR}" res)
	if(res EQUAL -1)
		set(${FLAGS_VAR} "${${FLAGS_VAR}} ${FLAG_VAR}" PARENT_SCOPE)
	endif()
endfunction()
