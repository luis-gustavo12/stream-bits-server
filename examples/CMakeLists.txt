# Every examples/<name>/ folder that has its own CMakeLists.txt is picked up automatically.
file(GLOB example_lists CONFIGURE_DEPENDS RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}" "*/CMakeLists.txt")

foreach (example_list IN LISTS example_lists)
    get_filename_component(example_dir "${example_list}" DIRECTORY)
    add_subdirectory("${example_dir}")
endforeach ()
