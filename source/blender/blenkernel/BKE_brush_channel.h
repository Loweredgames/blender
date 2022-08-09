#pragma once
#pragma once

/*
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 * The Original Code is Copyright (C) 2001-2002 by NaN Holding BV.
 * All rights reserved.
 */

/** \file
 * \ingroup bke
 * \brief New brush engine for sculpt
 */

#include "RNA_types.h"

/*
The new brush engine is based on command lists.  These lists
will eventually be created by a node editor.

Key is the concept of BrushChannels.  A brush channel is
a logical parameter with a type, input settings (e.g. pen),
a falloff curve, etc.

Brush channels have a concept of inheritance.  There is a
BrushChannelSet (collection of channels) in Sculpt,
in Brush, and in BrushCommand.  Inheritence behavior
is controller via BrushChannel->flag.

This should completely replace UnifiedPaintSettings.
*/

#include "BLI_compiler_compat.h"
#include "DNA_brush_channel_types.h"
#include "DNA_texture_types.h"

#ifdef __cplusplus
extern "C" {
#endif

struct BrushChannel;
struct BlendWriter;
struct StructRNA;
struct BlendDataReader;
struct BlendLibReader;
struct ID;
struct BlendExpander;
struct Brush;
struct Sculpt;
struct LibraryForeachIDData;
struct ToolSettings;
struct UnifiedPaintSettings;

#define make_builtin_ch_name(idname) BRUSH_BUILTIN_##idname

//#define DEBUG_CURVE_MAPPING_ALLOC
#ifdef DEBUG_CURVE_MAPPING_ALLOC
void namestack_push(const char *name);
void *namestack_pop(void *passthru);
#endif

typedef void (*BrushChannelIDCallback)(void *userdata,
                                       struct ID *id,
                                       BrushChannelSet *chset,
                                       BrushChannel *ch);
/* TODO: clean up this struct */
typedef struct BrushMappingDef {
  int curve;
  bool enabled;
  bool inv;
  bool inherit;
  float min, max;
  int blendmode;
  float func_cutoff;
  float factor;  // if 0, will default to 1.0
  bool no_default;
} BrushMappingDef;

typedef struct BrushMappingPreset {
  // must match order of BRUSH_MAPPING_XXX enums
  struct BrushMappingDef pressure, xtilt, ytilt, angle, speed, random, stroke_t;
} BrushMappingPreset;

/* input mapping data */
typedef struct BrushMappingData {
  float pressure, xtilt, ytilt, angle, speed, random, stroke_t;
} BrushMappingData;

#define MAX_BRUSH_ENUM_DEF 32

/* copy of PropertyEnumItem only with static char arrays instead of pointers
   for strings */
typedef struct BrushEnumDef {
  int value;
  const char identifier[64];
  char icon[32];  // don't forget when writing literals that icon here is a string, not an int!
  const char name[64];
  const char description[512];
} BrushEnumDef;

/*
  Defines a brush channel.  Includes limits, UI data,
  default values, etc.
*/
typedef struct BrushChannelType {
  char uiname[128], idname[64], tooltip[512], category[128];
  float min, max, soft_min, soft_max;
  BrushMappingPreset mappings;

  int type, flag, ui_flag;
  int subtype;

  bool user_defined; /* this is a user-defined channel; currently unused */
} BrushChannelType;

BrushChannelSet *BKE_brush_channelset_create();
void BKE_brush_channelset_free(BrushChannelSet *chset);
void BKE_brush_channelset_ensure_channels(BrushChannelSet *chset, int sculpt_tool);
void BKE_brush_channelset_blend_read(BrushChannelSet *chset, struct BlendDataReader *reader);
void BKE_brush_channelset_blend_write(BrushChannelSet *chset, struct BlendWriter *writer);
/*
set up static type checker for BKE_brush_channel_XXX name-checking macros
*/
#define BRUSH_CHANNEL_DEFINE_EXTERNAL
#include "intern/brush_channel_define.h"
#undef BRUSH_CHANNEL_DEFINE_EXTERNAL

BrushChannel *_BKE_brush_channelset_ensure(BrushChannelSet *chset, const char *idname);
#define BKE_brush_channelset_ensure(chset, idname) \
  _BKE_brush_channelset_ensure(chset, make_builtin_ch_name(idname))

BrushChannel *_BKE_brush_channelset_lookup(BrushChannelSet *chset, const char *idname);
#define BKE_brush_channelset_lookup(chset, channel) \
  _BKE_brush_channelset_lookup(chset, make_builtin_ch_name(channel))

bool _BKE_brush_channelset_has(BrushChannelSet *chset, const char *idname);
#define BKE_brush_channelset_has(chset, channel) \
  _BKE_brush_channelset_has(chset, make_builtin_ch_name(channel))

/* Flags all channels with BRUSH_CHANNEL_NEEDS_EVALUATE so we
   reevaluate values from RNA */
void BKE_brush_channelset_begin(BrushChannelSet *chset, BrushChannelType *type);

float _BKE_brush_channelset_eval_float(const struct Brush *br,
                                       const struct Scene *scene,
                                       const char *idname,
                                       BrushMappingData *mapping);
int _BKE_brush_channelset_eval_int(const struct Brush *br,
                                   const struct Scene *scene,
                                   const char *idname,
                                   BrushMappingData *mapping);
float _BKE_brush_channelset_float_get(BrushChannelSet *chset, const char *idname);
void _BKE_brush_channelset_float_set(BrushChannelSet *chset, const char *idname, float f);
int _BKE_brush_channelset_int_get(BrushChannelSet *chset, const char *idname);
void _BKE_brush_channelset_int_set(BrushChannelSet *chset, const char *idname, int i);

#define BKE_brush_channelset_eval_float(br, scene, channel, mapdata) \
  _BKE_brush_channelset_eval_float(br, scene, make_builtin_ch_name(channel), mapdata)
#define BKE_brush_channelset_eval_int(br, scene, channel, mapdata) \
  _BKE_brush_channelset_eval_int(br, scene, make_builtin_ch_name(channel), mapdata)

#define BKE_brush_channelset_float_get(chset, idname) \
  _BKE_brush_channelset_float_get(chset, make_builtin_ch_name(idname))
#define BKE_brush_channelset_float_set(chset, idname, f) \
  _BKE_brush_channelset_float_set(chset, make_builtin_ch_name(idname), f)
#define BKE_brush_channelset_int_get(chset, idname) \
  _BKE_brush_channelset_int_get(chset, make_builtin_ch_name(idname))
#define BKE_brush_channelset_int_set(chset, idname, f) \
  _BKE_brush_channelset_int_set(chset, make_builtin_ch_name(idname), f)

BrushChannelSet *BKE_brush_channelset_copy(BrushChannelSet *chset);

/* Create a (copied) final brush channel set with all inheritance and unified flags
   and input mappings taken into account. */
BrushChannelSet *BKE_brush_channelset_create_final(const struct Brush *brush,
                                                   const struct Scene *scene,
                                                   BrushMappingData *mapdata);

BLI_INLINE const char *BKE_brush_mapping_type_to_typename(eBrushMappingType type)
{
  switch (type) {
    case BRUSH_MAPPING_PRESSURE:
      return "PRESSURE";
    case BRUSH_MAPPING_ANGLE:
      return "ANGLE";
    case BRUSH_MAPPING_SPEED:
      return "SPEED";
    case BRUSH_MAPPING_XTILT:
      return "XTILT";
    case BRUSH_MAPPING_YTILT:
      return "YTILT";
    case BRUSH_MAPPING_RANDOM:
      return "RANDOM";
    case BRUSH_MAPPING_STROKE_T:
      return "DISTANCE";
    default:
      return "Error";
  }
}

const char *BKE_brush_channel_category_get(BrushChannel *ch);
const void BKE_brush_channel_category_set(BrushChannel *ch, const char *category);
bool BKE_brush_channel_inherits(const struct Brush *brush,
                                const struct ToolSettings *tool_settings,
                                BrushChannel *ch);
BrushChannelSet *BKE_brush_channelset_get_final(const struct Brush *brush,
                                                const struct ToolSettings *tool_settings);

void BKE_brush_channelset_toolsettings_init(struct ToolSettings *ts);

/* Get rna path for brush channel. Calling code should call MEM_SAFE_FREE on result. */
char *BKE_brush_channel_rna_path(ID *owner, BrushChannel *ch);

void _BKE_brush_channelset_mark_update(BrushChannelSet *chset, const char *idname);
#define BKE_brush_channelset_mark_update(chset, idname) \
  _BKE_brush_channelset_mark_update(chset, make_builtin_ch_name(idname))

/* Disable optimization for a function (for debugging use only!)*/
#ifdef __clang__
#  define ATTR_NO_OPT __attribute__((optnone))
#elif defined(_MSC_VER)
#  define ATTR_NO_OPT __pragma(optimize("", off))
#elif defined(__GNUC__)
#  define ATTR_NO_OPT __attribute__((optimize("O0")))
#else
#  define ATTR_NO_OPT
#endif

#ifdef __cplusplus
}
#endif
