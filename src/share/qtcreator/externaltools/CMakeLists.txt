set(EXT_TOOLS_DIR "${IDE_DATA_PATH}/externaltools")

set(tools qmlscene.xml qml.xml)

if (WIN32)
  list(APPEND tools notepad_win.xml)
elseif (APPLE)
  list(APPEND tools vi_mac.xml)
else()
  list(APPEND tools vi.xml)
endif()

qtc_copy_to_builddir(copy_src_share_to_builddir
  FILES ${tools}
  DESTINATION "${EXT_TOOLS_DIR}"
)

install(FILES ${tools}
  DESTINATION "${EXT_TOOLS_DIR}"
)

qtc_add_sbom(ExternalToolsResources
  TYPE RESOURCES
)

qtc_sbom_add_files(ExternalToolsResources
  FILES "${tools}"
  FILE_TYPE "RESOURCES"
  INSTALL_PATH "${EXT_TOOLS_DIR}"
)
