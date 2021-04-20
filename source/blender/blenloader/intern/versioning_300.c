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
 */

/** \file
 * \ingroup blenloader
 */
/* allow readfile to use deprecated functionality */
#define DNA_DEPRECATED_ALLOW

#include "DNA_gpencil_types.h"

#include "BKE_main.h"

#include "BLO_readfile.h"
#include "readfile.h"

void do_versions_after_linking_300(Main *bmain, ReportList *UNUSED(reports))
{
  if (!MAIN_VERSION_ATLEAST(bmain, 300, 0)) {
    /* Grease Pencil Masking Intersect. */
    Scene *scene = bmain->scenes.first;
    if (scene != NULL) {
      LISTBASE_FOREACH (Object *, ob, &bmain->objects) {
        if (ob->type != OB_GPENCIL) {
          continue;
        }
        bGPdata *gpd = ob->data;
        LISTBASE_FOREACH (bGPDlayer *, gpl, &gpd->layers) {
          LISTBASE_FOREACH (bGPDlayer_Mask *, mask, &gpl->mask_layers) {
            mask->flag |= GP_MASK_INTERSECT;
          }
        }
      }
    }
  }

  /**
   * Versioning code until next subversion bump goes here.
   *
   * \note Be sure to check when bumping the version:
   * - #blo_do_versions_300 in this file.
   * - "versioning_userdef.c", #blo_do_versions_userdef
   * - "versioning_userdef.c", #do_versions_theme
   *
   * \note Keep this message at the bottom of the function.
   */
  {
    /* Keep this block, even when empty. */
  }
}

/* NOLINTNEXTLINE: readability-function-size */
void blo_do_versions_300(FileData *fd, Library *UNUSED(lib), Main *UNUSED(bmain))
{
  UNUSED_VARS(fd);
  /**
   * Versioning code until next subversion bump goes here.
   *
   * \note Be sure to check when bumping the version:
   * - "versioning_userdef.c", #blo_do_versions_userdef
   * - "versioning_userdef.c", #do_versions_theme
   *
   * \note Keep this message at the bottom of the function.
   */
  {
    /* Keep this block, even when empty. */
  }
}
