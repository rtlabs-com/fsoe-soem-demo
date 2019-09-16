
set(SOURCES fsoe_sample.c)
add_executable(fsoe_sample ${SOURCES})
target_link_libraries(fsoe_sample soem)
install(TARGETS fsoe_sample DESTINATION bin)
