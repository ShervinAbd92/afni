add_executable(${NIFTI_PACKAGE_PREFIX}fsl_api_driver fsl_api_driver.c)
target_link_libraries(
  ${NIFTI_PACKAGE_PREFIX}fsl_api_driver PUBLIC ${NIFTI_PACKAGE_PREFIX}fslio
)
target_include_directories(
  ${NIFTI_PACKAGE_PREFIX}fsl_api_driver
  PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
         $<INSTALL_INTERFACE:${NIFTI_INSTALL_INCLUDE_DIR}>
)

if(NOT NIFTI_INSTALL_NO_LIBRARIES)
  install(
    TARGETS ${NIFTI_PACKAGE_PREFIX}fsl_api_driver
    EXPORT NIFTITargets
    RUNTIME DESTINATION ${NIFTI_INSTALL_RUNTIME_DIR} COMPONENT RuntimeLibraries
    LIBRARY DESTINATION ${NIFTI_INSTALL_LIBRARY_DIR} COMPONENT RuntimeLibraries
    ARCHIVE DESTINATION ${NIFTI_INSTALL_LIBRARY_DIR} COMPONENT Development
    INCLUDES DESTINATION ${NIFTI_INSTALL_INCLUDE_DIR}
  )
endif()
