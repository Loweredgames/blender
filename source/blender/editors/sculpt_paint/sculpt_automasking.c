/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2020 Blender Foundation. All rights reserved. */

/** \file
 * \ingroup edsculpt
 */

#include "MEM_guardedalloc.h"

#include "BLI_blenlib.h"
#include "BLI_hash.h"
#include "BLI_math.h"
#include "BLI_task.h"

#include "DNA_brush_types.h"
#include "DNA_mesh_types.h"
#include "DNA_meshdata_types.h"

#include "BKE_brush.h"
#include "BKE_context.h"
#include "BKE_mesh.h"
#include "BKE_mesh_mapping.h"
#include "BKE_object.h"
#include "BKE_paint.h"
#include "BKE_pbvh.h"
#include "BKE_scene.h"

#include "DEG_depsgraph.h"

#include "WM_api.h"
#include "WM_message.h"
#include "WM_toolsystem.h"
#include "WM_types.h"

#include "ED_object.h"
#include "ED_screen.h"
#include "ED_sculpt.h"
#include "paint_intern.h"
#include "sculpt_intern.h"

#include "RNA_access.h"
#include "RNA_define.h"

#include "bmesh.h"

#include <math.h>
#include <stdlib.h>

AutomaskingCache *SCULPT_automasking_active_cache_get(SculptSession *ss)
{
  if (ss->cache) {
    return ss->cache->automasking;
  }
  if (ss->filter_cache) {
    return ss->filter_cache->automasking;
  }
  return NULL;
}

bool SCULPT_is_automasking_mode_enabled(const Sculpt *sd,
                                        const Brush *br,
                                        const eAutomasking_flag mode)
{
  if (br) {
    return br->automasking_flags & mode || sd->automasking_flags & mode;
  }
  return sd->automasking_flags & mode;
}

bool SCULPT_is_automasking_enabled(const Sculpt *sd, const SculptSession *ss, const Brush *br)
{
  if (br && SCULPT_stroke_is_dynamic_topology(ss, br)) {
    return false;
  }
  if (SCULPT_is_automasking_mode_enabled(sd, br, BRUSH_AUTOMASKING_TOPOLOGY)) {
    return true;
  }
  if (SCULPT_is_automasking_mode_enabled(sd, br, BRUSH_AUTOMASKING_FACE_SETS)) {
    return true;
  }
  if (SCULPT_is_automasking_mode_enabled(sd, br, BRUSH_AUTOMASKING_BOUNDARY_EDGES)) {
    return true;
  }
  if (SCULPT_is_automasking_mode_enabled(sd, br, BRUSH_AUTOMASKING_BOUNDARY_FACE_SETS)) {
    return true;
  }
  if (SCULPT_is_automasking_mode_enabled(sd, br, BRUSH_AUTOMASKING_CAVITY)) {
    return true;
  }

  return false;
}

static int sculpt_automasking_mode_effective_bits(const Sculpt *sculpt, const Brush *brush)
{
  if (brush) {
    return sculpt->automasking_flags | brush->automasking_flags;
  }
  return sculpt->automasking_flags;
}

static bool SCULPT_automasking_needs_factors_cache(const Sculpt *sd, const Brush *brush)
{

  const int automasking_flags = sculpt_automasking_mode_effective_bits(sd, brush);
  if (automasking_flags & BRUSH_AUTOMASKING_TOPOLOGY) {
    return true;
  }
  if (automasking_flags & (BRUSH_AUTOMASKING_BOUNDARY_FACE_SETS |
                           BRUSH_AUTOMASKING_BOUNDARY_EDGES | BRUSH_AUTOMASKING_CAVITY)) {
    return brush && brush->automasking_boundary_edges_propagation_steps != 1;
  }
  return false;
}

float SCULPT_calc_cavity(SculptSession *ss, const int vertex)
{
  SculptVertexNeighborIter ni;
  const float *co = SCULPT_vertex_co_get(ss, vertex);
  float avg[3];
  float elen = 0.0f;
  float e_num = 0.0f;

  zero_v3(avg);

  SCULPT_VERTEX_NEIGHBORS_ITER_BEGIN (ss, vertex, ni) {
    const float *co2 = SCULPT_vertex_co_get(ss, ni.index);

    elen += len_v3v3(co, co2);
    e_num += 1.0f;
    add_v3_v3(avg, co2);
  }
  SCULPT_VERTEX_NEIGHBORS_ITER_END(ni);

  if (e_num == 0.0f) {
    return 0.0f;
  }

  mul_v3_fl(avg, 1.0f / (float)e_num);
  elen /= e_num;

  float no[3];

  SCULPT_vertex_normal_get(ss, vertex, no);

  sub_v3_v3(avg, co);

  /* Use distance to plane. */
  float factor = dot_v3v3(avg, no) / elen;

  return factor;
}

static float sculpt_automasking_cavity_factor(AutomaskingCache *automasking,
                                              SculptSession *ss,
                                              int vert)
{
  float factor = SCULPT_calc_cavity(ss, vert);
  float sign = signf(factor);

  factor = fabsf(factor) * automasking->settings.cavity_factor * 50.0f;

  factor = factor * sign * 0.5f + 0.5f;
  CLAMP(factor, 0.0f, 1.0f);

  return (automasking->settings.flags & BRUSH_AUTOMASKING_CAVITY_INVERT) ? 1.0f - factor : factor;
}

float SCULPT_automasking_factor_get(AutomaskingCache *automasking, SculptSession *ss, int vert)
{
  if (!automasking) {
    return 1.0f;
  }
  /* If the cache is initialized with valid info, use the cache. This is used when the
   * automasking information can't be computed in real time per vertex and needs to be
   * initialized for the whole mesh when the stroke starts. */
  if (automasking->factor) {
    return automasking->factor[vert];
  }

  if (automasking->settings.flags & BRUSH_AUTOMASKING_FACE_SETS) {
    if (!SCULPT_vertex_has_face_set(ss, vert, automasking->settings.initial_face_set)) {
      return 0.0f;
    }
  }

  if (automasking->settings.flags & BRUSH_AUTOMASKING_BOUNDARY_EDGES) {
    if (SCULPT_vertex_is_boundary(ss, vert)) {
      return 0.0f;
    }
  }

  if (automasking->settings.flags & BRUSH_AUTOMASKING_BOUNDARY_FACE_SETS) {
    if (!SCULPT_vertex_has_unique_face_set(ss, vert)) {
      return 0.0f;
    }
  }

  if (automasking->settings.flags & BRUSH_AUTOMASKING_CAVITY) {
    return sculpt_automasking_cavity_factor(automasking, ss, vert);
  }

  return 1.0f;
}

void SCULPT_automasking_cache_free(AutomaskingCache *automasking)
{
  if (!automasking) {
    return;
  }

  MEM_SAFE_FREE(automasking->factor);
  MEM_SAFE_FREE(automasking);
}

static bool sculpt_automasking_is_constrained_by_radius(Brush *br)
{
  /* 2D falloff is not constrained by radius. */
  if (br->falloff_shape == PAINT_FALLOFF_SHAPE_TUBE) {
    return false;
  }

  if (ELEM(br->sculpt_tool, SCULPT_TOOL_GRAB, SCULPT_TOOL_THUMB, SCULPT_TOOL_ROTATE)) {
    return true;
  }
  return false;
}

typedef struct AutomaskFloodFillData {
  float *automask_factor;
  float radius;
  bool use_radius;
  float location[3];
  char symm;
} AutomaskFloodFillData;

static bool automask_floodfill_cb(
    SculptSession *ss, int from_v, int to_v, bool UNUSED(is_duplicate), void *userdata)
{
  AutomaskFloodFillData *data = userdata;

  data->automask_factor[to_v] = 1.0f;
  data->automask_factor[from_v] = 1.0f;
  return (!data->use_radius ||
          SCULPT_is_vertex_inside_brush_radius_symm(
              SCULPT_vertex_co_get(ss, to_v), data->location, data->radius, data->symm));
}

static float *SCULPT_topology_automasking_init(Sculpt *sd, Object *ob, float *automask_factor)
{
  SculptSession *ss = ob->sculpt;
  Brush *brush = BKE_paint_brush(&sd->paint);

  if (BKE_pbvh_type(ss->pbvh) == PBVH_FACES && !ss->pmap) {
    BLI_assert_msg(0, "Topology masking: pmap missing");
    return NULL;
  }

  const int totvert = SCULPT_vertex_count_get(ss);
  for (int i = 0; i < totvert; i++) {
    automask_factor[i] = 0.0f;
  }

  /* Flood fill automask to connected vertices. Limited to vertices inside
   * the brush radius if the tool requires it. */
  SculptFloodFill flood;
  SCULPT_floodfill_init(ss, &flood);
  const float radius = ss->cache ? ss->cache->radius : FLT_MAX;
  SCULPT_floodfill_add_active(sd, ob, ss, &flood, radius);

  AutomaskFloodFillData fdata = {
      .automask_factor = automask_factor,
      .radius = radius,
      .use_radius = ss->cache && sculpt_automasking_is_constrained_by_radius(brush),
      .symm = SCULPT_mesh_symmetry_xyz_get(ob),
  };
  copy_v3_v3(fdata.location, SCULPT_active_vertex_co_get(ss));
  SCULPT_floodfill_execute(ss, &flood, automask_floodfill_cb, &fdata);
  SCULPT_floodfill_free(&flood);

  return automask_factor;
}

static float *sculpt_face_sets_automasking_init(Sculpt *sd, Object *ob, float *automask_factor)
{
  SculptSession *ss = ob->sculpt;
  Brush *brush = BKE_paint_brush(&sd->paint);

  if (!SCULPT_is_automasking_enabled(sd, ss, brush)) {
    return NULL;
  }

  if (BKE_pbvh_type(ss->pbvh) == PBVH_FACES && !ss->pmap) {
    BLI_assert_msg(0, "Face Sets automasking: pmap missing");
    return NULL;
  }

  int tot_vert = SCULPT_vertex_count_get(ss);
  int active_face_set = SCULPT_active_face_set_get(ss);
  for (int i = 0; i < tot_vert; i++) {
    if (!SCULPT_vertex_has_face_set(ss, i, active_face_set)) {
      automask_factor[i] *= 0.0f;
    }
  }

  return automask_factor;
}

static float *sculpt_cavity_automasking_init(Sculpt *sd,
                                             Object *ob,
                                             AutomaskingCache *automasking,
                                             float *automask_factor,
                                             int boundary_propagation_steps)
{
  SculptSession *ss = ob->sculpt;
  Brush *brush = BKE_paint_brush(&sd->paint);
  float *factor = automask_factor, *scratch;

  if (!SCULPT_is_automasking_enabled(sd, ss, brush)) {
    return NULL;
  }

  if (BKE_pbvh_type(ss->pbvh) == PBVH_FACES && !ss->pmap) {
    BLI_assert_msg(0, "Cavity mask automasking: pmap missing");
    return NULL;
  }

  int tot_vert = SCULPT_vertex_count_get(ss);

  /* Prepare a few scratch arrays for blur if necassary. */
  if (boundary_propagation_steps > 1) {
    factor = MEM_malloc_arrayN(tot_vert, sizeof(float), __func__);
    scratch = MEM_malloc_arrayN(tot_vert, sizeof(float), __func__);

    for (int i = 0; i < tot_vert; i++) {
      factor[i] = scratch[i] = 1.0f;
    }
  }

  for (int i = 0; i < tot_vert; i++) {
    factor[i] *= sculpt_automasking_cavity_factor(automasking, ss, i);
  }

  /* Blur. */
  for (int iteration = 0; iteration < boundary_propagation_steps - 1; iteration++) {
    SWAP(float *, factor, scratch);

    for (int i = 0; i < tot_vert; i++) {
      SculptVertexNeighborIter ni;
      float sum = 0.0f;
      int tot = 0;

      SCULPT_VERTEX_NEIGHBORS_ITER_BEGIN (ss, i, ni) {
        sum += scratch[ni.index];
        tot++;
      }
      SCULPT_VERTEX_NEIGHBORS_ITER_END(ni);

      if (tot) {
        sum /= tot;
      }

      factor[i] = (sum + factor[i]) * 0.5f;
    }
  }

  if (boundary_propagation_steps > 1) {
    for (int i = 0; i < tot_vert; i++) {
      automask_factor[i] *= factor[i];
    }

    MEM_SAFE_FREE(factor);
    MEM_SAFE_FREE(scratch);
  }

  return automask_factor;
}

#define EDGE_DISTANCE_INF -1

float *SCULPT_boundary_automasking_init(Object *ob,
                                        eBoundaryAutomaskMode mode,
                                        int propagation_steps,
                                        float *automask_factor)
{
  SculptSession *ss = ob->sculpt;

  if (!ss->pmap) {
    BLI_assert_msg(0, "Boundary Edges masking: pmap missing");
    return NULL;
  }

  const int totvert = SCULPT_vertex_count_get(ss);
  int *edge_distance = MEM_callocN(sizeof(int) * totvert, "automask_factor");

  for (int i = 0; i < totvert; i++) {
    edge_distance[i] = EDGE_DISTANCE_INF;
    switch (mode) {
      case AUTOMASK_INIT_BOUNDARY_EDGES:
        if (SCULPT_vertex_is_boundary(ss, i)) {
          edge_distance[i] = 0;
        }
        break;
      case AUTOMASK_INIT_BOUNDARY_FACE_SETS:
        if (!SCULPT_vertex_has_unique_face_set(ss, i)) {
          edge_distance[i] = 0;
        }
        break;
    }
  }

  for (int propagation_it = 0; propagation_it < propagation_steps; propagation_it++) {
    for (int i = 0; i < totvert; i++) {
      if (edge_distance[i] != EDGE_DISTANCE_INF) {
        continue;
      }
      SculptVertexNeighborIter ni;
      SCULPT_VERTEX_NEIGHBORS_ITER_BEGIN (ss, i, ni) {
        if (edge_distance[ni.index] == propagation_it) {
          edge_distance[i] = propagation_it + 1;
        }
      }
      SCULPT_VERTEX_NEIGHBORS_ITER_END(ni);
    }
  }

  for (int i = 0; i < totvert; i++) {
    if (edge_distance[i] == EDGE_DISTANCE_INF) {
      continue;
    }
    const float p = 1.0f - ((float)edge_distance[i] / (float)propagation_steps);
    const float edge_boundary_automask = pow2f(p);
    automask_factor[i] *= (1.0f - edge_boundary_automask);
  }

  MEM_SAFE_FREE(edge_distance);
  return automask_factor;
}

static void SCULPT_automasking_cache_settings_update(AutomaskingCache *automasking,
                                                     SculptSession *ss,
                                                     Sculpt *sd,
                                                     Brush *brush)
{
  automasking->settings.flags = sculpt_automasking_mode_effective_bits(sd, brush);
  automasking->settings.initial_face_set = SCULPT_active_face_set_get(ss);
  automasking->settings.cavity_factor = brush->automasking_cavity_factor;
}

AutomaskingCache *SCULPT_automasking_cache_init(Sculpt *sd, Brush *brush, Object *ob)
{
  SculptSession *ss = ob->sculpt;
  const int totvert = SCULPT_vertex_count_get(ss);

  if (!SCULPT_is_automasking_enabled(sd, ss, brush)) {
    return NULL;
  }

  AutomaskingCache *automasking = MEM_callocN(sizeof(AutomaskingCache), "automasking cache");
  SCULPT_automasking_cache_settings_update(automasking, ss, sd, brush);
  SCULPT_boundary_info_ensure(ob);

  if (!SCULPT_automasking_needs_factors_cache(sd, brush)) {
    return automasking;
  }

  automasking->factor = MEM_malloc_arrayN(totvert, sizeof(float), "automask_factor");
  for (int i = 0; i < totvert; i++) {
    automasking->factor[i] = 1.0f;
  }

  const int boundary_propagation_steps = brush ?
                                             brush->automasking_boundary_edges_propagation_steps :
                                             1;

  if (SCULPT_is_automasking_mode_enabled(sd, brush, BRUSH_AUTOMASKING_TOPOLOGY)) {
    SCULPT_vertex_random_access_ensure(ss);
    SCULPT_topology_automasking_init(sd, ob, automasking->factor);
  }
  if (SCULPT_is_automasking_mode_enabled(sd, brush, BRUSH_AUTOMASKING_FACE_SETS)) {
    SCULPT_vertex_random_access_ensure(ss);
    sculpt_face_sets_automasking_init(sd, ob, automasking->factor);
  }

  if (SCULPT_is_automasking_mode_enabled(sd, brush, BRUSH_AUTOMASKING_CAVITY)) {
    SCULPT_vertex_random_access_ensure(ss);
    sculpt_cavity_automasking_init(
        sd, ob, automasking, automasking->factor, boundary_propagation_steps);
  }

  if (SCULPT_is_automasking_mode_enabled(sd, brush, BRUSH_AUTOMASKING_BOUNDARY_EDGES)) {
    SCULPT_vertex_random_access_ensure(ss);
    SCULPT_boundary_automasking_init(
        ob, AUTOMASK_INIT_BOUNDARY_EDGES, boundary_propagation_steps, automasking->factor);
  }
  if (SCULPT_is_automasking_mode_enabled(sd, brush, BRUSH_AUTOMASKING_BOUNDARY_FACE_SETS)) {
    SCULPT_vertex_random_access_ensure(ss);
    SCULPT_boundary_automasking_init(
        ob, AUTOMASK_INIT_BOUNDARY_FACE_SETS, boundary_propagation_steps, automasking->factor);
  }

  return automasking;
}
