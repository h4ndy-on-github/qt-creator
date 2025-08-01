if (CMAKE_VERSION GREATER_EQUAL 3.19)
  set(QTC_COMMAND_ERROR_IS_FATAL COMMAND_ERROR_IS_FATAL ANY)
endif()

if (CMAKE_VERSION VERSION_LESS 3.18)
  if (CMAKE_CXX_COMPILER_ID STREQUAL GNU)
    set(BUILD_WITH_PCH OFF CACHE BOOL "" FORCE)
  endif()
endif()

if (CMAKE_VERSION VERSION_GREATER_EQUAL 3.18)
  include(CheckLinkerFlag)
endif()

include(FeatureSummary)

#
# Default Qt compilation defines
#

list(APPEND DEFAULT_DEFINES
  QT_CREATOR
  QT_NO_JAVA_STYLE_ITERATORS
  QT_NO_CAST_TO_ASCII QT_RESTRICTED_CAST_FROM_ASCII QT_NO_FOREACH
  QT_DISABLE_DEPRECATED_UP_TO=0x050900
  QT_WARN_DEPRECATED_UP_TO=0x060400
  QT_USE_QSTRINGBUILDER
)

if (WIN32)
  list(APPEND DEFAULT_DEFINES UNICODE _UNICODE _CRT_SECURE_NO_WARNINGS)

  if (NOT BUILD_WITH_PCH)
    list(APPEND DEFAULT_DEFINES
      WIN32_LEAN_AND_MEAN
      WINVER=0x0A00
      _WIN32_WINNT=0x0A00
    )
  endif()
endif()

#
# Setup path handling
#

if (APPLE)
  set(_IDE_APP_PATH ".")
  set(_IDE_APP_TARGET "${IDE_DISPLAY_NAME}")

  set(_IDE_OUTPUT_PATH "${_IDE_APP_TARGET}.app/Contents")

  set(_IDE_LIBRARY_BASE_PATH "Frameworks")
  set(_IDE_LIBRARY_PATH "${_IDE_OUTPUT_PATH}/${_IDE_LIBRARY_BASE_PATH}")
  set(_IDE_PLUGIN_PATH "${_IDE_OUTPUT_PATH}/PlugIns/qtcreator")
  set(_IDE_LIBEXEC_PATH "${_IDE_OUTPUT_PATH}/Resources/libexec")
  set(_IDE_DATA_PATH "${_IDE_OUTPUT_PATH}/Resources")
  set(_IDE_DOC_PATH "${_IDE_OUTPUT_PATH}/Resources/doc")
  set(_IDE_BIN_PATH "${_IDE_OUTPUT_PATH}/MacOS")
  set(_IDE_LIBRARY_ARCHIVE_PATH "${_IDE_LIBRARY_PATH}")

  set(_IDE_HEADER_INSTALL_PATH "${_IDE_DATA_PATH}/Headers/qtcreator")
  set(_IDE_CMAKE_INSTALL_PATH "${_IDE_DATA_PATH}/lib/cmake")
elseif(WIN32)
  set(_IDE_APP_PATH "bin")
  set(_IDE_APP_TARGET "${IDE_ID}")

  set(_IDE_LIBRARY_BASE_PATH "lib")
  set(_IDE_LIBRARY_PATH "${_IDE_LIBRARY_BASE_PATH}/qtcreator")
  set(_IDE_PLUGIN_PATH "${_IDE_LIBRARY_BASE_PATH}/qtcreator/plugins")
  set(_IDE_LIBEXEC_PATH "bin")
  set(_IDE_DATA_PATH "share/qtcreator")
  set(_IDE_DOC_PATH "share/doc/qtcreator")
  set(_IDE_BIN_PATH "bin")
  set(_IDE_LIBRARY_ARCHIVE_PATH "${_IDE_BIN_PATH}")

  set(_IDE_HEADER_INSTALL_PATH "include/qtcreator")
  set(_IDE_CMAKE_INSTALL_PATH "lib/cmake")
else ()
  # Small hack to silence a warning in the stable branch - but it means the value is incorrect
  if (NOT CMAKE_LIBRARY_ARCHITECTURE AND NOT CMAKE_INSTALL_LIBDIR)
    set(CMAKE_INSTALL_LIBDIR "lib")
  endif()
  include(GNUInstallDirs)
  set(_IDE_APP_PATH "${CMAKE_INSTALL_BINDIR}")
  set(_IDE_APP_TARGET "${IDE_ID}")

  set(_IDE_LIBRARY_BASE_PATH "${CMAKE_INSTALL_LIBDIR}")
  set(_IDE_LIBRARY_PATH "${_IDE_LIBRARY_BASE_PATH}/qtcreator")
  set(_IDE_PLUGIN_PATH "${_IDE_LIBRARY_BASE_PATH}/qtcreator/plugins")
  set(_IDE_LIBEXEC_PATH "${CMAKE_INSTALL_LIBEXECDIR}/qtcreator")
  set(_IDE_DATA_PATH "${CMAKE_INSTALL_DATAROOTDIR}/qtcreator")
  set(_IDE_DOC_PATH "${CMAKE_INSTALL_DATAROOTDIR}/doc/qtcreator")
  set(_IDE_BIN_PATH "${CMAKE_INSTALL_BINDIR}")
  set(_IDE_LIBRARY_ARCHIVE_PATH "${_IDE_LIBRARY_PATH}")

  set(_IDE_HEADER_INSTALL_PATH "include/qtcreator")
  set(_IDE_CMAKE_INSTALL_PATH "${_IDE_LIBRARY_BASE_PATH}/cmake")
endif ()

file(RELATIVE_PATH _PLUGIN_TO_LIB "/${_IDE_PLUGIN_PATH}" "/${_IDE_LIBRARY_PATH}")
file(RELATIVE_PATH _PLUGIN_TO_QT "/${_IDE_PLUGIN_PATH}" "/${_IDE_LIBRARY_BASE_PATH}/Qt/lib")
file(RELATIVE_PATH _LIB_TO_QT "/${_IDE_LIBRARY_PATH}" "/${_IDE_LIBRARY_BASE_PATH}/Qt/lib")

if (APPLE)
  set(_RPATH_BASE "@executable_path")
  set(_LIB_RPATH "@loader_path")
  set(_PLUGIN_RPATH "@loader_path;@loader_path/${_PLUGIN_TO_LIB}")
elseif (WIN32)
  set(_RPATH_BASE "")
  set(_LIB_RPATH "")
  set(_PLUGIN_RPATH "")
else()
  set(_RPATH_BASE "\$ORIGIN")
  set(_LIB_RPATH "\$ORIGIN;\$ORIGIN/${_LIB_TO_QT}")
  set(_PLUGIN_RPATH "\$ORIGIN;\$ORIGIN/${_PLUGIN_TO_LIB};\$ORIGIN/${_PLUGIN_TO_QT}")
endif ()

set(__QTC_PLUGINS "" CACHE INTERNAL "*** Internal ***")
set(__QTC_LIBRARIES "" CACHE INTERNAL "*** Internal ***")
set(__QTC_EXECUTABLES "" CACHE INTERNAL "*** Internal ***")
set(__QTC_TESTS "" CACHE INTERNAL "*** Internal ***")
set(__QTC_RESOURCE_FILES "" CACHE INTERNAL "*** Internal ***")

# handle SCCACHE hack
# SCCACHE does not work with the /Zi option, which makes each compilation write debug info
# into the same .pdb file - even with /FS, which usually makes this work in the first place.
# Replace /Zi with /Z7, which leaves the debug info in the object files until link time.
# This increases memory usage, disk space usage and linking time, so should only be
# enabled if necessary.
# Must be called after project(...).
function(qtc_handle_compiler_cache_support)
  if (WITH_SCCACHE_SUPPORT OR WITH_CCACHE_SUPPORT)
    if (MSVC)
      foreach(config DEBUG RELWITHDEBINFO)
        foreach(lang C CXX)
          set(flags_var "CMAKE_${lang}_FLAGS_${config}")
          string(REPLACE "/Zi" "/Z7" ${flags_var} "${${flags_var}}")
          set(${flags_var} "${${flags_var}}" PARENT_SCOPE)
        endforeach()
      endforeach()
    endif()
  endif()
  if (WITH_CCACHE_SUPPORT)
    find_program(CCACHE_PROGRAM ccache)
    if(CCACHE_PROGRAM)
      set(CMAKE_CXX_COMPILER_LAUNCHER "${CCACHE_PROGRAM}" CACHE STRING "CXX compiler launcher" FORCE)
      set(CMAKE_C_COMPILER_LAUNCHER "${CCACHE_PROGRAM}" CACHE STRING "C compiler launcher" FORCE)
    endif()
  endif()
endfunction()

function(qtc_handle_llvm_linker)
  if (QTC_USE_LLVM_LINKER)
    find_program(LLVM_LINK_PROGRAM llvm-link)
    if(LLVM_LINK_PROGRAM)
      set(CMAKE_LINKER "${LLVM_LINK_PROGRAM}" CACHE STRING "LLVM linker" FORCE)
    endif()
  endif()
endfunction()

function(qtc_enable_release_for_debug_configuration)
  if (MSVC)
    string(REPLACE "/Od" "/O2" CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG}")
    string(REPLACE "/Ob0" "/Ob1" CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG}")
    string(REPLACE "/RTC1" ""  CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG}")
  else()
    set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -O2")
  endif()
  set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG}" PARENT_SCOPE)
endfunction()

function(qtc_enable_sanitize _target _sanitize_flags)
  target_compile_options("${_target}" PUBLIC -fsanitize=${SANITIZE_FLAGS})

  if (CMAKE_CXX_COMPILER_ID MATCHES "Clang" OR CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    target_link_options("${_target}" PUBLIC -fsanitize=${SANITIZE_FLAGS})
  endif()
endfunction()

function(qtc_deeper_concept_diagnostic_depth _target)
  target_compile_options("${_target}" PRIVATE $<$<COMPILE_LANG_AND_ID:CXX,GNU>:-fconcepts-diagnostics-depth=8>)
endfunction()

function(qtc_add_link_flags_no_undefined target)
  # needs CheckLinkerFlags
  if (CMAKE_VERSION VERSION_GREATER_EQUAL 3.18 AND NOT MSVC AND NOT APPLE)
    set(no_undefined_flag "-Wl,--no-undefined")
    check_linker_flag(CXX ${no_undefined_flag} QTC_LINKER_SUPPORTS_NO_UNDEFINED)
    if (NOT QTC_LINKER_SUPPORTS_NO_UNDEFINED)
        set(no_undefined_flag "-Wl,-undefined,error")
        check_linker_flag(CXX ${no_undefined_flag} QTC_LINKER_SUPPORTS_UNDEFINED_ERROR)
        if (NOT QTC_LINKER_SUPPORTS_UNDEFINED_ERROR)
            return()
        endif()
    endif()
    target_link_options("${target}" PRIVATE "${no_undefined_flag}")
  endif()
endfunction()

function(append_extra_translations target_name)
  if(NOT ARGN)
    return()
  endif()

  if(TARGET "${target_name}")
    get_target_property(_input "${target_name}" QT_EXTRA_TRANSLATIONS)
    if (_input)
      set(_output "${_input}" "${ARGN}")
    else()
      set(_output "${ARGN}")
    endif()
    set_target_properties("${target_name}" PROPERTIES QT_EXTRA_TRANSLATIONS "${_output}")
  endif()
endfunction()

function(update_cached_list name value)
  set(_tmp_list "${${name}}")
  list(APPEND _tmp_list "${value}")
  set("${name}" "${_tmp_list}" CACHE INTERNAL "*** Internal ***")
endfunction()

function(set_explicit_moc target_name file)
  unset(file_dependencies)
  if (file MATCHES "^.*plugin.h$")
    set(file_dependencies DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/${target_name}.json")
  endif()
  set_property(SOURCE "${file}" PROPERTY SKIP_AUTOMOC ON)
  qt_wrap_cpp(file_moc "${file}" ${file_dependencies})
  target_sources(${target_name} PRIVATE "${file_moc}")
endfunction()

function(set_public_headers target sources)
  foreach(source IN LISTS sources)
    if (source MATCHES "\.h$|\.hpp$")
      qtc_add_public_header(${source})
    endif()
  endforeach()
endfunction()

function(update_resource_files_list sources)
  foreach(source IN LISTS sources)
    if (source MATCHES "\.qrc$")
      get_filename_component(resource_name ${source} NAME_WE)
      string(REPLACE "-" "_" resource_name ${resource_name})
      update_cached_list(__QTC_RESOURCE_FILES "${resource_name}")
    endif()
  endforeach()
endfunction()

function(set_public_includes target includes system)
  foreach(inc_dir IN LISTS includes)
    if (NOT IS_ABSOLUTE ${inc_dir})
      set(inc_dir "${CMAKE_CURRENT_SOURCE_DIR}/${inc_dir}")
    endif()
    file(RELATIVE_PATH include_dir_relative_path ${PROJECT_SOURCE_DIR} ${inc_dir})
    target_include_directories(${target} ${system} PUBLIC
      $<BUILD_INTERFACE:${inc_dir}>
      $<INSTALL_INTERFACE:${_IDE_HEADER_INSTALL_PATH}/${include_dir_relative_path}>
    )
  endforeach()
endfunction()

function(finalize_test_setup test_name)
  cmake_parse_arguments(_arg "" "TIMEOUT" "" ${ARGN})
  if (DEFINED _arg_TIMEOUT)
    set(timeout_arg TIMEOUT ${_arg_TIMEOUT})
  else()
    set(timeout_arg)
  endif()
  # Never translate tests:
  set_tests_properties(${name}
    PROPERTIES
      QT_SKIP_TRANSLATION ON
      ${timeout_arg}
  )

  if (WIN32)
    list(APPEND env_path $ENV{PATH})
    list(APPEND env_path ${CMAKE_BINARY_DIR}/${_IDE_PLUGIN_PATH})
    list(APPEND env_path ${CMAKE_BINARY_DIR}/${_IDE_BIN_PATH})
    # version-less target Qt::Test is an interface library that links to QtX::Test
    list(APPEND env_path $<TARGET_FILE_DIR:$<TARGET_PROPERTY:Qt::Test,INTERFACE_LINK_LIBRARIES>>)
    if (TARGET libclang)
        list(APPEND env_path $<TARGET_FILE_DIR:libclang>)
    endif()

    if (TARGET elfutils::elf)
        list(APPEND env_path $<TARGET_FILE_DIR:elfutils::elf>)
    endif()

    string(REPLACE "/" "\\" env_path "${env_path}")
    string(REPLACE ";" "\\;" env_path "${env_path}")

    set_tests_properties(${test_name} PROPERTIES ENVIRONMENT "PATH=${env_path}")
  endif()
endfunction()

function(check_qtc_disabled_targets target_name dependent_targets)
  foreach(dependency IN LISTS ${dependent_targets})
    foreach(type PLUGIN LIBRARY)
      string(TOUPPER "BUILD_${type}_${dependency}" build_target)
      if (DEFINED ${build_target} AND NOT ${build_target})
        message(SEND_ERROR "Target ${name} depends on ${dependency} which was disabled via ${build_target} set to ${${build_target}}")
      endif()
    endforeach()
  endforeach()
endfunction()

function(add_qtc_depends target_name)
  cmake_parse_arguments(_arg "" "" "PRIVATE;PUBLIC" ${ARGN})
  if (${_arg_UNPARSED_ARGUMENTS})
    message(FATAL_ERROR "add_qtc_depends had unparsed arguments")
  endif()

  check_qtc_disabled_targets(${target_name} _arg_PRIVATE)
  check_qtc_disabled_targets(${target_name} _arg_PUBLIC)

  set(depends "${_arg_PRIVATE}")
  set(public_depends "${_arg_PUBLIC}")

  target_link_libraries(${target_name} PRIVATE ${depends} PUBLIC ${public_depends})
endfunction()

function(check_library_dependencies)
  foreach(i ${ARGN})
    if (NOT TARGET ${i})
      continue()
    endif()
    get_property(_class_name TARGET "${i}" PROPERTY QTC_PLUGIN_CLASS_NAME)
    if (_class_name)
       message(SEND_ERROR "${i} is a plugin, not a library!")
    endif()
  endforeach()
endfunction()

function(enable_pch target)
  if (BUILD_WITH_PCH)
    # Skip PCH for targets that do not use the expected visibility settings:
    get_target_property(visibility_property "${target}" CXX_VISIBILITY_PRESET)
    get_target_property(inlines_property "${target}" VISIBILITY_INLINES_HIDDEN)

    if (NOT visibility_property STREQUAL "hidden" OR NOT inlines_property)
      return()
    endif()

    # static libs are maybe used by other projects, so they can not reuse same pch files
    if (MSVC)
        get_target_property(target_type "${target}" TYPE)
        if (target_type MATCHES "STATIC")
            return()
        endif()
    endif()

    # Skip PCH for targets that do not have QT_NO_CAST_TO_ASCII
    get_target_property(target_defines "${target}" COMPILE_DEFINITIONS)
    if (NOT "QT_NO_CAST_TO_ASCII" IN_LIST target_defines)
      return()
    endif()

    get_target_property(target_type ${target} TYPE)
    if (NOT ${target_type} STREQUAL "OBJECT_LIBRARY")
      function(_recursively_collect_dependencies input_target)
        get_target_property(input_type ${input_target} TYPE)
        if (${input_type} STREQUAL "INTERFACE_LIBRARY")
          set(prefix "INTERFACE_")
        endif()
        get_target_property(link_libraries ${input_target} ${prefix}LINK_LIBRARIES)
        foreach(library IN LISTS link_libraries)
          if(TARGET ${library} AND NOT ${library} IN_LIST dependencies)
            list(APPEND dependencies ${library})
            _recursively_collect_dependencies(${library})
          endif()
        endforeach()
        set(dependencies ${dependencies} PARENT_SCOPE)
      endfunction()
      _recursively_collect_dependencies(${target})

      function(_add_pch_target pch_target pch_file pch_dependency)
        if (EXISTS ${pch_file})
          add_library(${pch_target} STATIC
            ${CMAKE_CURRENT_BINARY_DIR}/empty_pch.cpp
            ${CMAKE_CURRENT_BINARY_DIR}/empty_pch.c)
          target_compile_definitions(${pch_target} PRIVATE ${DEFAULT_DEFINES})
          set_target_properties(${pch_target} PROPERTIES
            PRECOMPILE_HEADERS ${pch_file}
            CXX_VISIBILITY_PRESET hidden
            VISIBILITY_INLINES_HIDDEN ON
            CXX_EXTENSIONS OFF
            POSITION_INDEPENDENT_CODE ON
          )
          target_link_libraries(${pch_target} PRIVATE ${pch_dependency})

          if (WITH_SANITIZE)
            qtc_enable_sanitize("${pch_target}" ${SANITIZE_FLAGS})
          endif()
        endif()
      endfunction()

      if (NOT TARGET ${PROJECT_NAME}PchGui AND NOT TARGET ${PROJECT_NAME}PchConsole)
        file(GENERATE
          OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/empty_pch.c
          CONTENT "/*empty file*/")
        file(GENERATE
          OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/empty_pch.cpp
          CONTENT "/*empty file*/")
        set_source_files_properties(
            ${CMAKE_CURRENT_BINARY_DIR}/empty_pch.c
            ${CMAKE_CURRENT_BINARY_DIR}/empty_pch.cpp
            PROPERTIES GENERATED TRUE)

        _add_pch_target(${PROJECT_NAME}PchGui
          "${QtCreator_SOURCE_DIR}/src/shared/qtcreator_gui_pch.h" Qt::Widgets)
        _add_pch_target(${PROJECT_NAME}PchConsole
          "${QtCreator_SOURCE_DIR}/src/shared/qtcreator_pch.h" Qt::Core)
      endif()

      unset(PCH_TARGET)
      if ("Qt::Widgets" IN_LIST dependencies)
        set(PCH_TARGET ${PROJECT_NAME}PchGui)
      elseif ("Qt::Core" IN_LIST dependencies)
        set(PCH_TARGET ${PROJECT_NAME}PchConsole)
      endif()

      if (TARGET "${PCH_TARGET}")
        set_target_properties(${target} PROPERTIES
          PRECOMPILE_HEADERS_REUSE_FROM ${PCH_TARGET})
      endif()
    endif()
  endif()
endfunction()

function(condition_info varName condition)
  if (NOT ${condition})
    set(${varName} "" PARENT_SCOPE)
  else()
    string(REPLACE ";" " " _contents "${${condition}}")
    set(${varName} "with CONDITION ${_contents}" PARENT_SCOPE)
  endif()
endfunction()

function(extend_qtc_target target_name)
  set(opt_args "")
  set(single_args
    SOURCES_PREFIX
    SOURCES_PREFIX_FROM_TARGET
    FEATURE_INFO
  )
  set(multi_args
    CONDITION
    DEPENDS
    PUBLIC_DEPENDS
    DEFINES
    PUBLIC_DEFINES
    INCLUDES
    SYSTEM_INCLUDES
    PUBLIC_INCLUDES
    PUBLIC_SYSTEM_INCLUDES
    SOURCES
    EXPLICIT_MOC
    SKIP_AUTOMOC
    EXTRA_TRANSLATIONS
    PROPERTIES
    SOURCES_PROPERTIES
    PRIVATE_COMPILE_OPTIONS
    PUBLIC_COMPILE_OPTIONS
    SBOM_ARGS
  )

  cmake_parse_arguments(_arg "${opt_args}" "${single_args}" "${multi_args}" ${ARGN})

  if (${_arg_UNPARSED_ARGUMENTS})
    message(FATAL_ERROR "extend_qtc_target had unparsed arguments")
  endif()

  condition_info(_extra_text _arg_CONDITION)
  if (NOT _arg_CONDITION)
    set(_arg_CONDITION ON)
  endif()
  if (${_arg_CONDITION})
    set(_feature_enabled ON)
  else()
    set(_feature_enabled OFF)
  endif()
  if (_arg_FEATURE_INFO)
    add_feature_info(${_arg_FEATURE_INFO} _feature_enabled "${_extra_text}")
  endif()
  if (NOT _feature_enabled)
    return()
  endif()

  if (_arg_SOURCES_PREFIX_FROM_TARGET)
    if (NOT TARGET ${_arg_SOURCES_PREFIX_FROM_TARGET})
      return()
    else()
      get_target_property(_arg_SOURCES_PREFIX ${_arg_SOURCES_PREFIX_FROM_TARGET} SOURCES_DIR)
    endif()
  endif()

  add_qtc_depends(${target_name}
    PRIVATE ${_arg_DEPENDS}
    PUBLIC ${_arg_PUBLIC_DEPENDS}
  )
  target_compile_definitions(${target_name}
    PRIVATE ${_arg_DEFINES}
    PUBLIC ${_arg_PUBLIC_DEFINES}
  )
  target_include_directories(${target_name} PRIVATE ${_arg_INCLUDES})
  target_include_directories(${target_name} SYSTEM PRIVATE ${_arg_SYSTEM_INCLUDES})

  set_public_includes(${target_name} "${_arg_PUBLIC_INCLUDES}" "")
  set_public_includes(${target_name} "${_arg_PUBLIC_SYSTEM_INCLUDES}" "SYSTEM")

  if (_arg_SOURCES_PREFIX)
    foreach(source IN LISTS _arg_SOURCES)
      list(APPEND prefixed_sources "${_arg_SOURCES_PREFIX}/${source}")
    endforeach()

    if (NOT IS_ABSOLUTE ${_arg_SOURCES_PREFIX})
      set(_arg_SOURCES_PREFIX "${CMAKE_CURRENT_SOURCE_DIR}/${_arg_SOURCES_PREFIX}")
    endif()
    target_include_directories(${target_name} PRIVATE $<BUILD_INTERFACE:${_arg_SOURCES_PREFIX}>)

    set(_arg_SOURCES ${prefixed_sources})
  endif()
  target_sources(${target_name} PRIVATE ${_arg_SOURCES})

  if (APPLE AND BUILD_WITH_PCH)
    foreach(source IN LISTS _arg_SOURCES)
      if (source MATCHES "^.*\.mm$")
        set_source_files_properties(${source} PROPERTIES SKIP_PRECOMPILE_HEADERS ON)
      endif()
    endforeach()
  endif()

  set_public_headers(${target_name} "${_arg_SOURCES}")
  update_resource_files_list("${_arg_SOURCES}")

  foreach(file IN LISTS _arg_EXPLICIT_MOC)
    set_explicit_moc(${target_name} "${file}")
  endforeach()

  foreach(file IN LISTS _arg_SKIP_AUTOMOC)
    set_property(SOURCE ${file} PROPERTY SKIP_AUTOMOC ON)
  endforeach()

  append_extra_translations(${target_name} "${_arg_EXTRA_TRANSLATIONS}")

  if (_arg_PROPERTIES)
    set_target_properties(${target_name} PROPERTIES ${_arg_PROPERTIES})
  endif()

  if (_arg_SOURCES_PROPERTIES)
    set_source_files_properties(${_arg_SOURCES} PROPERTIES ${_arg_SOURCES_PROPERTIES})
  endif()


  if (_arg_PRIVATE_COMPILE_OPTIONS)
      target_compile_options(${target_name} PRIVATE ${_arg_PRIVATE_COMPILE_OPTIONS})
  endif()

  if (_arg_PUBLIC_COMPILE_OPTIONS)
      target_compile_options(${target_name} PUBLIC ${_arg_PUBLIC_COMPILE_OPTIONS})
  endif()

  if(QT_GENERATE_SBOM AND _arg_SBOM_ARGS)
    qtc_extend_qtc_entity_sbom(${target_name} ${_arg_SBOM_ARGS})
  endif()
endfunction()

function (qtc_env_with_default envName varToSet default)
  if(DEFINED ENV{${envName}})
    set(${varToSet} $ENV{${envName}} PARENT_SCOPE)
  else()
    set(${varToSet} ${default} PARENT_SCOPE)
  endif()
endfunction()

# Checks whether any unparsed arguments have been passed to the function at the call site.
# Use this right after `cmake_parse_arguments`.
function(qtc_validate_all_args_are_parsed prefix)
  if(DEFINED ${prefix}_UNPARSED_ARGUMENTS)
    message(FATAL_ERROR "Unknown arguments: (${${prefix}_UNPARSED_ARGUMENTS})")
  endif()
endfunction()

# Defer calls command_name and its arguments to the end of the current add_subdirectory() scope.
function(qtc_defer_call command_name)
  cmake_language(EVAL CODE "cmake_language(DEFER CALL \"${command_name}\" ${ARGN}) ")
endfunction()

# Defer calls the function arguments to the end of the current add_subdirectory() scope.
# Also records that the target will be finalized, but has not been finalized yet. This is needed
# to properly handle SBOM generation for a project, where the project needs to be handled
# only after all the targets are finalized.
function(qtc_mark_for_deferred_finalization target command_name)
  set_property(GLOBAL APPEND PROPERTY _qtc_sbom_targets_expecting_finalization "${target}")

  qtc_defer_call("${command_name}" "${target}" ${ARGN})
endfunction()

# This function can remove single-value arguments from a list of arguments in order to pass
# a filtered list of arguments to a different function that uses
# cmake_parse_arguments.
# This is a stripped down version of _qt_internal_qt_remove_args.
# Parameters:
#   out_var: result of removing all arguments specified by ARGS_TO_REMOVE from ARGS
#   ARGS_TO_REMOVE: Arguments to remove.
#   ARGS: Arguments passed into the function, usually ${ARGV}
#   E.g.:
#   We want to forward all arguments from foo to bar, except INSTALL_PATH <path> since it will
#   trigger an error in bar.
#
#   foo(target BAR... INSTALL_PATH /tmp)
#   bar(target BAR...)
#
#   function(foo target)
#       cmake_parse_arguments(PARSE_ARGV 1 arg "" "INSTALL_PATH" "BAR)
#       qtc_remove_single_args(forward_args
#           ARGS_TO_REMOVE INSTALL_PATH
#           ARGS ${ARGV}
#       )
#       bar(${target} ${forward_args})
#   endfunction()
function(qtc_remove_single_args out_var)
  set(opt_args "")
  set(single_args "")
  set(multi_args
    ARGS
    ARGS_TO_REMOVE
  )

  cmake_parse_arguments(PARSE_ARGV 0 arg "${opt_args}" "${single_args}" "${multi_args}")

  set(out_args ${arg_ARGS})

  foreach(arg IN LISTS arg_ARGS_TO_REMOVE)
    # Find arg.
    list(FIND out_args ${arg} index_to_remove)
    if(index_to_remove EQUAL -1)
      continue()
    endif()

    # Remove arg.
    list(REMOVE_AT out_args ${index_to_remove})

    list(LENGTH out_args result_len)
    if(index_to_remove EQUAL result_len)
        # We removed the last argument.
        continue()
    endif()

    # Remove arg value.
    list(REMOVE_AT out_args ${index_to_remove})
  endforeach()

  set(${out_var} "${out_args}" PARENT_SCOPE)
endfunction()

# Helper function to forward options from one function to another.
#
# This is somewhat the opposite of _qt_internal_remove_args.
# It is a renamed copy of _qt_internal_forward_function_args from Qt. It's needed to support
# building Creator against older versions of Qt that don't have it.
#
# Parameters:
# FORWARD_PREFIX is usually arg because we pass cmake_parse_arguments(PARSE_ARGV 0 arg) in most code
# FORWARD_OPTIONS, FORWARD_SINGLE, FORWARD_MULTI are the options that should be forwarded.
#
# The forwarded args will be either set in arg_FORWARD_OUT_VAR or appended if FORWARD_APPEND is set.
#
# The function reads the options like ${arg_FORWARD_PREFIX}_${option} in the parent scope.
function(qtc_forward_function_args)
    set(opt_args
        FORWARD_APPEND
    )
    set(single_args
        FORWARD_PREFIX
    )
    set(multi_args
        FORWARD_OPTIONS
        FORWARD_SINGLE
        FORWARD_MULTI
        FORWARD_OUT_VAR
    )
    cmake_parse_arguments(PARSE_ARGV 0 arg "${opt_args}" "${single_args}" "${multi_args}")
    _qt_internal_validate_all_args_are_parsed(arg)

    if(NOT arg_FORWARD_OUT_VAR)
        message(FATAL_ERROR "FORWARD_OUT_VAR must be provided.")
    endif()

    set(forward_args "")
    foreach(option_name IN LISTS arg_FORWARD_OPTIONS)
        if(${arg_FORWARD_PREFIX}_${option_name})
            list(APPEND forward_args "${option_name}")
        endif()
    endforeach()

    foreach(option_name IN LISTS arg_FORWARD_SINGLE)
        if(NOT "${${arg_FORWARD_PREFIX}_${option_name}}" STREQUAL "")
            list(APPEND forward_args "${option_name}" "${${arg_FORWARD_PREFIX}_${option_name}}")
        endif()
    endforeach()

    foreach(option_name IN LISTS arg_FORWARD_MULTI)
        if(NOT "${${arg_FORWARD_PREFIX}_${option_name}}" STREQUAL "")
            list(APPEND forward_args "${option_name}" ${${arg_FORWARD_PREFIX}_${option_name}})
        endif()
    endforeach()

    if(arg_FORWARD_APPEND)
        set(forward_args ${${arg_FORWARD_OUT_VAR}} "${forward_args}")
    endif()

    set(${arg_FORWARD_OUT_VAR} "${forward_args}" PARENT_SCOPE)
endfunction()
