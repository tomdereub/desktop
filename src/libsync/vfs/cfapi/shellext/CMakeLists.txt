include(ECMAddAppIcon)

if (NOT DEFINED RESOURCES_FILE_NAME)
    set(RESOURCES_FILE_NAME ${CFAPI_SHELL_EXTENSIONS_LIB_NAME})
endif()

set(theme_dir ${CMAKE_SOURCE_DIR}/theme)
set(custom_state_icons_path "${theme_dir}/cfapishellext_custom_states")
set(CUSTOM_STATE_ICON_SHARED "${custom_state_icons_path}/shared.svg")
set(CUSTOM_STATE_ICON_LOCKED "${custom_state_icons_path}/locked.svg")

# Generate png icons from svg
find_program(SVG_CONVERTER
  NAMES inkscape inkscape.exe rsvg-convert
  REQUIRED
  HINTS "C:\\Program Files\\Inkscape\\bin" "/usr/bin" ENV SVG_CONVERTER_DIR)
# REQUIRED keyword is only supported on CMake 3.18 and above
if (NOT SVG_CONVERTER)
  message(FATAL_ERROR "Could not find a suitable svg converter. Set SVG_CONVERTER_DIR to the path of either the inkscape or rsvg-convert executable.")
endif()

function(generate_sized_png_from_svg icon_path size)
  set(options)
  set(oneValueArgs OUTPUT_ICON_NAME OUTPUT_ICON_FULL_NAME_WLE OUTPUT_ICON_PATH)
  set(multiValueArgs)

  cmake_parse_arguments(ARG "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

  get_filename_component(icon_name_dir ${icon_path} DIRECTORY)
  get_filename_component(icon_name_wle ${icon_path} NAME_WLE)

  if (ARG_OUTPUT_ICON_NAME)
    set(icon_name_wle ${ARG_OUTPUT_ICON_NAME})
  endif ()

  if (ARG_OUTPUT_ICON_PATH)
    set(icon_name_dir ${ARG_OUTPUT_ICON_PATH})
  endif ()

  set(output_icon_full_name_wle "${size}-${icon_name_wle}")

  if (ARG_OUTPUT_ICON_FULL_NAME_WLE)
    set(output_icon_full_name_wle ${ARG_OUTPUT_ICON_FULL_NAME_WLE})
  endif ()

  if (EXISTS "${icon_name_dir}/${output_icon_full_name_wle}.png")
    return()
  endif()

  set(icon_output_name "${output_icon_full_name_wle}.png")
  message(STATUS "Generate ${icon_output_name}")
  execute_process(COMMAND
    "${SVG_CONVERTER}" -w ${size} -h ${size} "${icon_path}" -o "${icon_output_name}"
    WORKING_DIRECTORY "${icon_name_dir}"
    RESULT_VARIABLE
    SVG_CONVERTER_SIDEBAR_ERROR
    OUTPUT_QUIET
    ERROR_QUIET)

  if (SVG_CONVERTER_SIDEBAR_ERROR)
    message(FATAL_ERROR
      "${SVG_CONVERTER} could not generate icon: ${SVG_CONVERTER_SIDEBAR_ERROR}")
  else()
  endif()
endfunction()

foreach(size IN ITEMS 24;32;40;48;64;128;256;512;1024)
  get_filename_component(output_icon_name_custom_state_locked ${CUSTOM_STATE_ICON_LOCKED} NAME_WLE)
  generate_sized_png_from_svg(${CUSTOM_STATE_ICON_LOCKED} ${size} OUTPUT_ICON_NAME ${output_icon_name_custom_state_locked} OUTPUT_ICON_PATH "${custom_state_icons_path}/")
endforeach()

foreach(size IN ITEMS 24;32;40;48;64;128;256;512;1024)
  get_filename_component(output_icon_name_custom_state_shared ${CUSTOM_STATE_ICON_SHARED} NAME_WLE)
  generate_sized_png_from_svg(${CUSTOM_STATE_ICON_SHARED} ${size} OUTPUT_ICON_NAME ${output_icon_name_custom_state_shared} OUTPUT_ICON_PATH "${custom_state_icons_path}/")
endforeach()

set(RC_DEPENDENCIES "")

file(GLOB_RECURSE CUSTOM_STATE_ICONS_LOCKED "${custom_state_icons_path}/*-locked*")
set(CUSTOM_STATE_ICON_LOCKED_NAME "locked")
set(RC_DEPENDENCIES "${RC_DEPENDENCIES} ${CUSTOM_STATE_ICON_LOCKED_NAME}.ico")
ecm_add_app_icon(CUSTOM_STATE_ICON_LOCKED_OUT ICONS "${CUSTOM_STATE_ICONS_LOCKED}" SIDEBAR_ICONS "" OUTFILE_BASENAME "${CUSTOM_STATE_ICON_LOCKED_NAME}" ICON_INDEX 1)

file(GLOB_RECURSE CUSTOM_STATE_ICONS_SHARED "${custom_state_icons_path}/*-shared*")
set(CUSTOM_STATE_ICON_SHARED_NAME "shared")
set(RC_DEPENDENCIES "${RC_DEPENDENCIES} ${CUSTOM_STATE_ICON_SHARED_NAME}.ico")
ecm_add_app_icon(CUSTOM_STATE_ICON_SHARED_OUT ICONS "${CUSTOM_STATE_ICONS_SHARED}" SIDEBAR_ICONS "" OUTFILE_BASENAME "${CUSTOM_STATE_ICON_SHARED_NAME}" ICON_INDEX 2)

function(merge_files IN_FILE OUT_FILE)
  file(READ ${IN_FILE} CONTENTS)
  message("Merging ${IN_FILE} into ${OUT_FILE}")
  file(APPEND ${OUT_FILE} "${CONTENTS}")
endfunction()


get_filename_component(RC_IN_FOLDER ${CUSTOM_STATE_ICON_SHARED_OUT}} DIRECTORY)

file(REMOVE "${RC_IN_FOLDER}/${CFAPI_SHELL_EXTENSIONS_LIB_NAME}.rc.in")

file(GLOB_RECURSE RC_IN_FILES "${RC_IN_FOLDER}/*rc.in")

foreach(rc_in_file IN ITEMS ${RC_IN_FILES})
  get_filename_component(rc_in_file_name ${rc_in_file} NAME)
  merge_files(${rc_in_file} "${RC_IN_FOLDER}/${CFAPI_SHELL_EXTENSIONS_LIB_NAME}.rc.in")
endforeach()

add_custom_command(
  OUTPUT "${RC_IN_FOLDER}/${CFAPI_SHELL_EXTENSIONS_LIB_NAME}.rc"
  COMMAND ${CMAKE_COMMAND}
  ARGS -E copy "${RC_IN_FOLDER}/${CFAPI_SHELL_EXTENSIONS_LIB_NAME}.rc.in" "${RC_IN_FOLDER}/${CFAPI_SHELL_EXTENSIONS_LIB_NAME}.rc"
  DEPENDS "locked.ico" "shared.ico"
  WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
)

message("CUSTOM_STATE_ICON_SHARED: ${CUSTOM_STATE_ICON_SHARED}")

# Windows SDK command-line tools require native paths
file(TO_NATIVE_PATH "${CMAKE_CURRENT_SOURCE_DIR}" MidleFileFolder)
set(GeneratedFilesPath "${MidleFileFolder}\\Generated")
set(MidlOutputPathHeader "${GeneratedFilesPath}\\CustomStateProvider.g.h")
set(MidlOutputPathTlb "${GeneratedFilesPath}\\CustomStateProvider.tlb")
set(MidlOutputPathWinmd "${GeneratedFilesPath}\\CustomStateProvider.winmd")

add_custom_target(CustomStateProviderImpl
   DEPENDS ${MidlOutputPathHeader}
)

if(NOT DEFINED ENV{WindowsSdkDir})
    message("Getting WindowsSdkDir from Registry")
    get_filename_component(WindowsSdkDir "[HKEY_LOCAL_MACHINE\\SOFTWARE\\WOW6432Node\\Microsoft\\Windows Kits\\Installed Roots;KitsRoot10]" ABSOLUTE)
else()
    set(WindowsSdkDir $ENV{WindowsSdkDir})
    message("Setting WindowsSdkDir from ENV{WindowsSdkDir")
endif()

# we need cmake path to work with subfolders
file(TO_CMAKE_PATH "${WindowsSdkDir}" WindowsSdkDir)

MACRO(SUBDIRLIST result curdir)
  FILE(GLOB children RELATIVE ${curdir} ${curdir}/*)
  SET(dirlist "")
  FOREACH(child ${children})
    IF(IS_DIRECTORY ${curdir}/${child})
      LIST(APPEND dirlist ${child})
    ENDIF()
  ENDFOREACH()
  SET(${result} ${dirlist})
ENDMACRO()

SUBDIRLIST(WindowsSdkList "${WindowsSdkDir}/bin")

# pick only dirs that start with 10.0
list(FILTER WindowsSdkList INCLUDE REGEX "10.0.")
# sort the list of subdirs and choose the latest
list(SORT WindowsSdkList ORDER ASCENDING)
list(GET WindowsSdkList -1 WindowsSdkLatest)
message("WindowsSdkLatest has been set to: ${WindowsSdkLatest}")

if(NOT WindowsSdkLatest)
    message( FATAL_ERROR "Windows SDK not found")
endif()

SUBDIRLIST(listFoundationContracts "${WindowsSdkDir}/References/${WindowsSdkLatest}/Windows.Foundation.FoundationContract")
list(FILTER listFoundationContracts INCLUDE REGEX "[0-9]+\.")
list(SORT listFoundationContracts ORDER ASCENDING)
list(GET listFoundationContracts -1 WindowsFoundationContractVersion)
message("WindowsFoundationContractVersion has been set to: ${WindowsFoundationContractVersion}")

if(NOT WindowsFoundationContractVersion)
    message( FATAL_ERROR "Windows Foundation Contract is not found in ${WindowsSdkLatest} SDK.")
endif()

SUBDIRLIST(listCloudFilesContracts "${WindowsSdkDir}/References/${WindowsSdkLatest}/Windows.Storage.Provider.CloudFilesContract")
list(FILTER listCloudFilesContracts INCLUDE REGEX "[0-9]+\.")
list(SORT listCloudFilesContracts ORDER ASCENDING)
list(GET listCloudFilesContracts -1 WindowsStorageProviderCloudFilesContractVersion)
message("WindowsStorageProviderCloudFilesContractVersion has been set to: ${WindowsStorageProviderCloudFilesContractVersion}")

if(NOT WindowsStorageProviderCloudFilesContractVersion)
    message( FATAL_ERROR "Windows Storage Provider Cloud Files Contract is not found in ${WindowsSdkLatest} SDK.")
endif()

# we no longer need to work with sub folders, so convert the WindowsSdkDir to native path
file(TO_NATIVE_PATH ${WindowsSdkDir} WindowsSdkDir)
message("WindowsSdkDir has been set to: ${WindowsSdkDir}")
message("WindowsSdkList has been set to: ${WindowsSdkList}")
message("WindowsSdkLatest has been set to: ${WindowsSdkLatest}")

set(TargetPlatform "x64")
if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(TargetPlatform "x64")
elseif(CMAKE_SIZEOF_VOID_P EQUAL 4)
    set(TargetPlatform "x86")
endif()

set(WindowsSDKReferencesPath "${WindowsSdkDir}\\References\\${WindowsSdkLatest}")
set(WindowsSDKBinPathForTools "${WindowsSdkDir}\\bin\\${WindowsSdkLatest}\\${TargetPlatform}")
set(WindowsSDKMetadataDirectory "${WindowsSdkDir}\\UnionMetadata\\${WindowsSdkLatest}")

IF(NOT EXISTS "${WindowsSDKReferencesPath}" OR NOT IS_DIRECTORY "${WindowsSDKReferencesPath}")
    message( FATAL_ERROR "Please install Windows SDK ${WindowsSdkLatest}")
ENDIF()
IF(NOT EXISTS "${WindowsSDKBinPathForTools}" OR NOT IS_DIRECTORY "${WindowsSDKBinPathForTools}")
    message( FATAL_ERROR "Please install Windows SDK ${WindowsSdkLatest}")
ENDIF()
IF(NOT EXISTS "${WindowsSDKMetadataDirectory}" OR NOT IS_DIRECTORY "${WindowsSDKMetadataDirectory}")
    message( FATAL_ERROR "Please install Windows SDK ${WindowsSdkLatest}")
ENDIF()
set(midlExe "${WindowsSDKBinPathForTools}\\midl.exe")
set(cppWinRtExe "${WindowsSDKBinPathForTools}\\cppwinrt.exe")

message("cppWinRtExe: ${cppWinRtExe}")
message("midlExe: ${midlExe}")

# use midl.exe and cppwinrt.exe to generate files for CustomStateProvider (WinRT class)
add_custom_command(OUTPUT ${MidlOutputPathHeader}
   COMMAND ${midlExe} /winrt /h nul /tlb ${MidlOutputPathTlb} /winmd ${MidlOutputPathWinmd} /metadata_dir "${WindowsSDKReferencesPath}\\Windows.Foundation.FoundationContract\\${WindowsFoundationContractVersion}" /nomidl /reference "${WindowsSDKReferencesPath}\\Windows.Foundation.FoundationContract\\${WindowsFoundationContractVersion}\\Windows.Foundation.FoundationContract.winmd" /reference "${WindowsSDKReferencesPath}\\Windows.Storage.Provider.CloudFilesContract\\${WindowsStorageProviderCloudFilesContractVersion}\\Windows.Storage.Provider.CloudFilesContract.winmd" /I ${MidleFileFolder} customstateprovider.idl
   COMMAND ${cppWinRtExe} -in ${MidlOutputPathWinmd} -comp ${GeneratedFilesPath} -pch pch.h -ref ${WindowsSDKMetadataDirectory} -out ${GeneratedFilesPath} -verbose
   COMMENT "Creating generated files from customstateprovider.idl"
)

add_library(CfApiShellExtensions MODULE
    dllmain.cpp
    cfapishellintegrationclassfactory.cpp
    customstateprovideripc.cpp
    thumbnailprovider.cpp
	thumbnailprovideripc.cpp
    ${CMAKE_SOURCE_DIR}/src/common/shellextensionutils.cpp
    customstateprovider.cpp
    CfApiShellIntegration.def
    "${RC_IN_FOLDER}/${CFAPI_SHELL_EXTENSIONS_LIB_NAME}.rc"
)

add_dependencies(CfApiShellExtensions CustomStateProviderImpl)

target_link_libraries(CfApiShellExtensions shlwapi Gdiplus Nextcloud::csync Qt5::Core Qt5::Network)

target_include_directories(CfApiShellExtensions PRIVATE ${GeneratedFilesPath})

target_include_directories(CfApiShellExtensions PRIVATE ${CMAKE_SOURCE_DIR})

target_compile_features(CfApiShellExtensions PRIVATE cxx_std_17)

set_target_properties(CfApiShellExtensions
    PROPERTIES
        LIBRARY_OUTPUT_NAME
            ${CFAPI_SHELL_EXTENSIONS_LIB_NAME}
        RUNTIME_OUTPUT_NAME
            ${CFAPI_SHELL_EXTENSIONS_LIB_NAME}
        LIBRARY_OUTPUT_DIRECTORY
            ${BIN_OUTPUT_DIRECTORY}
        RUNTIME_OUTPUT_DIRECTORY
            ${BIN_OUTPUT_DIRECTORY}
)

install(TARGETS CfApiShellExtensions 
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_BINDIR}
)
