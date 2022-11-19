# vim: syntax=cmake

# ----------------------------------------------------------------------------
# add NormalRga implement
# ----------------------------------------------------------------------------
add_library(vproc_rga3 STATIC NormalRga.cpp NormalRgaApi.cpp)
set_target_properties(vproc_rga3 PROPERTIES FOLDER "mpp/vproc/rga3")

# TODO(lgY): move this to platform different file
add_definitions(-fPIC)
add_definitions(-DARMLINUX)
add_definitions(-Dlinux)
add_definitions(-DLINUX)
