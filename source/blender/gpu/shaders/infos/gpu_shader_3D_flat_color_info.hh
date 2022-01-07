
#include "gpu_shader_create_info.hh"

GPU_SHADER_INTERFACE_INFO(flat_color_iface, "").flat(Type::VEC4, "finalColor");

GPU_SHADER_CREATE_INFO(gpu_shader_3D_flat_color)
    .vertex_in(0, Type::VEC3, "pos")
    .vertex_in(1, Type::VEC4, "color")
    .vertex_out(flat_color_iface)
    .fragment_out(0, Type::VEC4, "fragColor")
    .push_constant(0, Type::MAT4, "ModelViewProjectionMatrix")
    .push_constant(1, Type::BOOL, "srgbTarget")
    .vertex_source("gpu_shader_3D_flat_color_vert.glsl")
    .fragment_source("gpu_shader_flat_color_frag.glsl")
    .additional_info("gpu_srgb_to_framebuffer_space")
    .do_static_compilation(true);

GPU_SHADER_CREATE_INFO(gpu_shader_3D_flat_color_clipped)
    .additional_info("gpu_shader_3D_flat_color")
    .additional_info("gpu_clip_planes")
    .do_static_compilation(true);
