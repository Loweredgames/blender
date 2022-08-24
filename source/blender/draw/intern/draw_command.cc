/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2022 Blender Foundation. */

/** \file
 * \ingroup draw
 */

#include "GPU_batch.h"
#include "GPU_compute.h"

#include "draw_command.hh"

#include <bitset>
#include <sstream>

namespace blender::draw::command {

/* -------------------------------------------------------------------- */
/** \name Commands Execution
 * \{ */

void ShaderBind::execute(RecordingState &state) const
{
  if (assign_if_different(state.shader, shader)) {
    GPU_shader_bind(shader);
  }
}

void ResourceBind::execute() const
{
  switch (type) {
    case ResourceBind::Type::Sampler:
      GPU_texture_bind_ex(is_reference ? *texture_ref : texture, sampler, slot, false);
      break;
    case ResourceBind::Type::Image:
      GPU_texture_image_bind(is_reference ? *texture_ref : texture, slot);
      break;
    case ResourceBind::Type::UniformBuf:
      GPU_uniformbuf_bind(is_reference ? *uniform_buf_ref : uniform_buf, slot);
      break;
    case ResourceBind::Type::StorageBuf:
      GPU_storagebuf_bind(is_reference ? *storage_buf_ref : storage_buf, slot);
      break;
  }
}

void PushConstant::execute(RecordingState &state) const
{
  switch (type) {
    case PushConstant::Type::IntValue:
      GPU_shader_uniform_vector_int(state.shader, location, comp_len, array_len, int4_value);
      break;
    case PushConstant::Type::IntReference:
      GPU_shader_uniform_vector_int(state.shader, location, comp_len, array_len, int_ref);
      break;
    case PushConstant::Type::FloatValue:
      GPU_shader_uniform_vector(state.shader, location, comp_len, array_len, float4_value);
      break;
    case PushConstant::Type::FloatReference:
      GPU_shader_uniform_vector(state.shader, location, comp_len, array_len, float_ref);
      break;
  }
}

void Draw::execute(RecordingState &state) const
{
  state.front_facing_set(handle.has_inverted_handedness());

  GPU_batch_set_shader(batch, state.shader);
  GPU_batch_draw_advanced(batch, vertex_first, vertex_len, 0, instance_len);
}

void DrawIndirect::execute(RecordingState &state) const
{
  state.front_facing_set(handle.has_inverted_handedness());

  GPU_batch_draw_indirect(batch, *indirect_buf, 0);
}

void Dispatch::execute(RecordingState &state) const
{
  if (is_reference) {
    GPU_compute_dispatch(state.shader, size_ref->x, size_ref->y, size_ref->z);
  }
  else {
    GPU_compute_dispatch(state.shader, size.x, size.y, size.z);
  }
}

void DispatchIndirect::execute(RecordingState &state) const
{
  GPU_compute_dispatch_indirect(state.shader, *indirect_buf);
}

void Barrier::execute() const
{
  GPU_memory_barrier(type);
}

void Clear::execute() const
{
  GPUFrameBuffer *fb = GPU_framebuffer_active_get();
  GPU_framebuffer_clear(fb, (eGPUFrameBufferBits)clear_channels, color, depth, stencil);
}

void StateSet::execute(RecordingState &recording_state) const
{
  /**
   * Does not support locked state for the moment and never should.
   * Better implement a less hacky selection!
   */
  BLI_assert(DST.state_lock == 0);

  if (!assign_if_different(recording_state.pipeline_state, new_state)) {
    return;
  }

  /* Keep old API working. Keep the state tracking in sync. */
  /* TODO(fclem): Move at the end of a pass. */
  DST.state = new_state;

  GPU_state_set(to_write_mask(new_state),
                to_blend(new_state),
                to_face_cull_test(new_state),
                to_depth_test(new_state),
                to_stencil_test(new_state),
                to_stencil_op(new_state),
                to_provoking_vertex(new_state));

  if (new_state & DRW_STATE_SHADOW_OFFSET) {
    GPU_shadow_offset(true);
  }
  else {
    GPU_shadow_offset(false);
  }

  /* TODO: this should be part of shader state. */
  if (new_state & DRW_STATE_CLIP_PLANES) {
    GPU_clip_distances(recording_state.view_clip_plane_count);
  }
  else {
    GPU_clip_distances(0);
  }

  if (new_state & DRW_STATE_IN_FRONT_SELECT) {
    /* XXX `GPU_depth_range` is not a perfect solution
     * since very distant geometries can still be occluded.
     * Also the depth test precision of these geometries is impaired.
     * However, it solves the selection for the vast majority of cases. */
    GPU_depth_range(0.0f, 0.01f);
  }
  else {
    GPU_depth_range(0.0f, 1.0f);
  }

  if (new_state & DRW_STATE_PROGRAM_POINT_SIZE) {
    GPU_program_point_size(true);
  }
  else {
    GPU_program_point_size(false);
  }
}

void StencilSet::execute() const
{
  GPU_stencil_write_mask_set(write_mask);
  GPU_stencil_compare_mask_set(compare_mask);
  GPU_stencil_reference_set(reference);
}

void DrawMulti::execute(RecordingState &state) const
{
  DrawMultiBuf::DrawCommandBuf &indirect_buf = multi_draw_buf->command_buf_;
  DrawMultiBuf::DrawGroupBuf &groups = multi_draw_buf->group_buf_;

  uint group_index = this->group_first;
  while (group_index != (uint)-1) {
    const DrawGroup &grp = groups[group_index];

    GPU_batch_set_shader(grp.gpu_batch, state.shader);

    constexpr intptr_t stride = sizeof(DrawCommand);
    intptr_t offset = stride * grp.command_start;

    /* Draw negatively scaled geometry first. */
    uint back_facing_len = grp.command_len - grp.front_facing_len;
    if (back_facing_len > 0) {
      state.front_facing_set(false);
      GPU_batch_draw_indirect(grp.gpu_batch, indirect_buf, offset);
      offset += stride * back_facing_len;
    }

    if (grp.front_facing_len > 0) {
      state.front_facing_set(true);
      GPU_batch_draw_indirect(grp.gpu_batch, indirect_buf, offset);
    }

    group_index = grp.next;
  }
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Commands Serialization for debugging
 * \{ */

std::string ShaderBind::serialize() const
{
  return std::string(".shader_bind(") + GPU_shader_get_name(shader) + ")";
}

std::string ResourceBind::serialize() const
{
  switch (type) {
    case Type::Sampler:
      return std::string(".bind_texture") + (is_reference ? "_ref" : "") + "(" +
             std::to_string(slot) +
             (sampler != GPU_SAMPLER_MAX ? std::string(", sampler=" + sampler) : "") + ")";
    case Type::Image:
      return std::string(".bind_image") + (is_reference ? "_ref" : "") + "(" +
             std::to_string(slot) + ")";
    case Type::UniformBuf:
      return std::string(".bind_uniform_buf") + (is_reference ? "_ref" : "") + "(" +
             std::to_string(slot) + ")";
    case Type::StorageBuf:
      return std::string(".bind_storage_buf") + (is_reference ? "_ref" : "") + "(" +
             std::to_string(slot) + ")";
    default:
      BLI_assert_unreachable();
      return "";
  }
}

std::string PushConstant::serialize() const
{
  std::stringstream ss;
  for (int i = 0; i < array_len; i++) {
    switch (comp_len) {
      case 1:
        switch (type) {
          case Type::IntValue:
            ss << int1_value;
            break;
          case Type::IntReference:
            ss << int_ref[i];
            break;
          case Type::FloatValue:
            ss << float1_value;
            break;
          case Type::FloatReference:
            ss << float_ref[i];
            break;
        }
        break;
      case 2:
        switch (type) {
          case Type::IntValue:
            ss << int2_value;
            break;
          case Type::IntReference:
            ss << int2_ref[i];
            break;
          case Type::FloatValue:
            ss << float2_value;
            break;
          case Type::FloatReference:
            ss << float2_ref[i];
            break;
        }
        break;
      case 3:
        switch (type) {
          case Type::IntValue:
            ss << int3_value;
            break;
          case Type::IntReference:
            ss << int3_ref[i];
            break;
          case Type::FloatValue:
            ss << float3_value;
            break;
          case Type::FloatReference:
            ss << float3_ref[i];
            break;
        }
        break;
      case 4:
        switch (type) {
          case Type::IntValue:
            ss << int4_value;
            break;
          case Type::IntReference:
            ss << int4_ref[i];
            break;
          case Type::FloatValue:
            ss << float4_value;
            break;
          case Type::FloatReference:
            ss << float4_ref[i];
            break;
        }
        break;
      case 16:
        switch (type) {
          case Type::IntValue:
          case Type::IntReference:
            BLI_assert_unreachable();
            break;
          case Type::FloatValue:
            ss << *reinterpret_cast<const float4x4 *>(&float4_value);
            break;
          case Type::FloatReference:
            ss << *float4x4_ref;
            break;
        }
        break;
    }
    if (i < array_len - 1) {
      ss << ", ";
    }
  }

  return std::string(".push_constant(") + std::to_string(location) + ", data=" + ss.str() + ")";
}

std::string Draw::serialize() const
{
  std::string inst_len = (instance_len == (uint)-1) ? "from_batch" : std::to_string(instance_len);
  std::string vert_len = (vertex_len == (uint)-1) ? "from_batch" : std::to_string(vertex_len);
  std::string vert_first = (vertex_first == (uint)-1) ? "from_batch" :
                                                        std::to_string(vertex_first);
  return std::string(".draw(inst_len=") + inst_len + ", vert_len=" + vert_len +
         ", vert_first=" + vert_first + ", res_id=" + std::to_string(handle.resource_index()) +
         ")";
}

std::string DrawIndirect::serialize() const
{
  return std::string(".draw_indirect()");
}

std::string Dispatch::serialize() const
{
  int3 sz = is_reference ? *size_ref : size;
  return std::string(".dispatch") + (is_reference ? "_ref" : "") + "(" + std::to_string(sz.x) +
         ", " + std::to_string(sz.y) + ", " + std::to_string(sz.z) + ")";
}

std::string DispatchIndirect::serialize() const
{
  return std::string(".dispatch_indirect()");
}

std::string Barrier::serialize() const
{
  /* TOOD(fclem): Better serialization... */
  return std::string(".barrier(") + std::to_string(type) + ")";
}

std::string Clear::serialize() const
{
  std::stringstream ss;
  if (eGPUFrameBufferBits(clear_channels) & GPU_COLOR_BIT) {
    ss << "color=" << color;
    if (eGPUFrameBufferBits(clear_channels) & (GPU_DEPTH_BIT | GPU_STENCIL_BIT)) {
      ss << ", ";
    }
  }
  if (eGPUFrameBufferBits(clear_channels) & GPU_DEPTH_BIT) {
    ss << "depth=" << depth;
    if (eGPUFrameBufferBits(clear_channels) & GPU_STENCIL_BIT) {
      ss << ", ";
    }
  }
  if (eGPUFrameBufferBits(clear_channels) & GPU_STENCIL_BIT) {
    ss << "stencil=0b" << std::bitset<8>(stencil) << ")";
  }
  return std::string(".clear(") + ss.str() + ")";
}

std::string StateSet::serialize() const
{
  /* TOOD(fclem): Better serialization... */
  return std::string(".state_set(") + std::to_string(new_state) + ")";
}

std::string StencilSet::serialize() const
{
  std::stringstream ss;
  ss << ".stencil_set(write_mask=0b" << std::bitset<8>(write_mask) << ", compare_mask=0b"
     << std::bitset<8>(compare_mask) << ", reference=0b" << std::bitset<8>(reference);
  return ss.str();
}

/** \} */

};  // namespace blender::draw::command
