# vim: syntax=cmake

# ----------------------------------------------------------------------------
# add rga implement
# ----------------------------------------------------------------------------
add_library(vproc_rga STATIC rga.cpp)
set_target_properties(vproc_rga PROPERTIES FOLDER "mpp/vproc/rga")
target_link_libraries(vproc_rga mpp_base)

add_subdirectory(test)
