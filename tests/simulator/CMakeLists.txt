cmake_minimum_required (VERSION 3.3)

project(test-simulator)

set(SOURCES_INC
)

set(SOURCES_CPP
  ${PROJECT_SOURCE_DIR}/initialize.cpp
)

if(HighFive)
  add_executable(${PROJECT_NAME} ${SOURCES_INC} ${SOURCES_CPP})

  target_include_directories(${PROJECT_NAME} PRIVATE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>
    ${GTEST_INCLUDE_DIRS}
  )

  target_link_directories(${PROJECT_NAME} PRIVATE
    ${HDF5_LIBRARY_PATH}
  )

  target_link_libraries(${PROJECT_NAME} PRIVATE
    phare_solver
    phare_diagnostic
    phare_initializer
    ${GTEST_LIBS}
    ${HDF5_LIBRARIES})

  add_phare_test(${PROJECT_NAME} ${CMAKE_CURRENT_BINARY_DIR})

endif()

