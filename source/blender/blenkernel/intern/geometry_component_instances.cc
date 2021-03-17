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

#include "BLI_float3.hh"
#include "BLI_float4x4.hh"
#include "BLI_map.hh"
#include "BLI_rand.hh"
#include "BLI_set.hh"
#include "BLI_span.hh"
#include "BLI_vector.hh"

#include "DNA_collection_types.h"

#include "BKE_attribute_access.hh"
#include "BKE_geometry_set.hh"

#include "attribute_access_intern.hh"

using blender::float3;
using blender::float4x4;
using blender::Map;
using blender::MutableSpan;
using blender::Set;
using blender::Span;
using blender::bke::ReadAttributePtr;

/* -------------------------------------------------------------------- */
/** \name Geometry Component Implementation
 * \{ */

InstancesComponent::InstancesComponent() : GeometryComponent(GEO_COMPONENT_TYPE_INSTANCES)
{
}

GeometryComponent *InstancesComponent::copy() const
{
  InstancesComponent *new_component = new InstancesComponent();
  new_component->transforms_ = transforms_;
  new_component->instanced_data_ = instanced_data_;
  return new_component;
}

void InstancesComponent::clear()
{
  instanced_data_.clear();
  transforms_.clear();
}

void InstancesComponent::add_instance(Object *object, float4x4 transform, const int id)
{
  InstancedData data;
  data.type = INSTANCE_DATA_TYPE_OBJECT;
  data.data.object = object;
  this->add_instance(data, transform, id);
}

void InstancesComponent::add_instance(Collection *collection, float4x4 transform, const int id)
{
  InstancedData data;
  data.type = INSTANCE_DATA_TYPE_COLLECTION;
  data.data.collection = collection;
  this->add_instance(data, transform, id);
}

void InstancesComponent::add_instance(InstancedData data, float4x4 transform, const int id)
{
  instanced_data_.append(data);
  transforms_.append(transform);
  ids_.append(id);
}

Span<InstancedData> InstancesComponent::instanced_data() const
{
  return instanced_data_;
}

Span<float4x4> InstancesComponent::transforms() const
{
  return transforms_;
}

Span<int> InstancesComponent::ids() const
{
  return ids_;
}

MutableSpan<float4x4> InstancesComponent::transforms()
{
  return transforms_;
}

int InstancesComponent::instances_amount() const
{
  const int size = instanced_data_.size();
  BLI_assert(transforms_.size() == size);
  return size;
}

bool InstancesComponent::is_empty() const
{
  return transforms_.size() == 0;
}

static blender::Array<int> generate_unique_instance_ids(Span<int> original_ids)
{
  using namespace blender;
  Array<int> unique_ids(original_ids.size());

  Set<int> used_unique_ids;
  used_unique_ids.reserve(original_ids.size());
  Vector<int> instances_with_id_collision;
  for (const int instance_index : original_ids.index_range()) {
    const int original_id = original_ids[instance_index];
    if (used_unique_ids.add(original_id)) {
      /* The original id has not been used by another instance yet. */
      unique_ids[instance_index] = original_id;
    }
    else {
      /* The original id of this instance collided with a previous instance, it needs to be looked
       * at again in a second pass. Don't generate a new random id here, because this might collide
       * with other existing ids. */
      instances_with_id_collision.append(instance_index);
    }
  }

  Map<int, RandomNumberGenerator> generator_by_original_id;
  for (const int instance_index : instances_with_id_collision) {
    const int original_id = original_ids[instance_index];
    RandomNumberGenerator &rng = generator_by_original_id.lookup_or_add_cb(original_id, [&]() {
      RandomNumberGenerator rng;
      rng.seed_random(original_id);
      return rng;
    });

    const int max_iteration = 100;
    for (int iteration = 0;; iteration++) {
      /* Try generating random numbers until an unused one has been found. */
      const int random_id = rng.get_int32();
      if (used_unique_ids.add(random_id)) {
        /* This random id is not used by another instance. */
        unique_ids[instance_index] = random_id;
        break;
      }
      if (iteration == max_iteration) {
        /* It seems to be very unlikely that we ever run into this case (assuming there are less
         * than 2^30 instances). However, if that happens, it's better to use an id that is not
         * unique than to be stuck in an infinite loop. */
        unique_ids[instance_index] = original_id;
        break;
      }
    }
  }

  return unique_ids;
}

blender::Span<int> InstancesComponent::almost_unique_ids() const
{
  std::lock_guard lock(almost_unique_ids_mutex_);
  if (almost_unique_ids_.size() != ids_.size()) {
    almost_unique_ids_ = generate_unique_instance_ids(ids_);
  }
  return almost_unique_ids_;
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Attribute Access
 * \{ */

int InstancesComponent::attribute_domain_size(const AttributeDomain domain) const
{
  BLI_assert(this->attribute_domain_supported(domain));
  switch (domain) {
    case ATTR_DOMAIN_POINT:
      return this->instances_amount();
    default:
      BLI_assert(false);
      break;
  }
  return 0;
}

namespace blender::bke {

static float3 get_matrix_position(const float4x4 &matrix)
{
  return matrix.translation();
}

static void set_matrix_position(float4x4 &matrix, const float3 &translation)
{
  copy_v3_v3(matrix.ptr()[3], translation);
}

static float3 get_matrix_rotation(const float4x4 &matrix)
{
  return matrix.to_euler();
}

static void set_matrix_rotation(float4x4 &matrix, const float3 &rotation)
{
  float4x4 rotation_matrix;
  loc_eul_size_to_mat4(rotation_matrix.values, float3(0), rotation, float3(1));
  matrix = matrix * rotation_matrix;
}

static float3 get_matrix_scale(const float4x4 &matrix)
{
  return matrix.scale();
}

static void set_matrix_scale(float4x4 &matrix, const float3 &scale)
{
  float4x4 scale_matrix;
  size_to_mat4(scale_matrix.values, scale);
  matrix = matrix * scale_matrix;
}

template<float3 (*GetFunc)(const float4x4 &), void (*SetFunc)(float4x4 &, const float3 &)>
class Float4x4AttributeProvider final : public BuiltinAttributeProvider {
 public:
  Float4x4AttributeProvider(std::string attribute_name)
      : BuiltinAttributeProvider(std::move(attribute_name),
                                 ATTR_DOMAIN_POINT,
                                 CD_PROP_FLOAT3,
                                 NonCreatable,
                                 Writable,
                                 NonDeletable)
  {
  }

  ReadAttributePtr try_get_for_read(const GeometryComponent &component) const final
  {
    const InstancesComponent &instances_component = static_cast<const InstancesComponent &>(
        component);
    if (instances_component.instances_amount() == 0) {
      return {};
    }

    return std::make_unique<DerivedArrayReadAttribute<float4x4, float3, GetFunc>>(
        ATTR_DOMAIN_POINT, instances_component.transforms());
  }

  WriteAttributePtr try_get_for_write(GeometryComponent &component) const final
  {
    InstancesComponent &instances_component = static_cast<InstancesComponent &>(component);
    if (instances_component.instances_amount() == 0) {
      return {};
    }

    return std::make_unique<DerivedArrayWriteAttribute<float4x4, float3, GetFunc, SetFunc>>(
        ATTR_DOMAIN_POINT, instances_component.transforms());
  }

  bool try_delete(GeometryComponent &UNUSED(component)) const final
  {
    return false;
  }

  bool try_create(GeometryComponent &UNUSED(component)) const final
  {
    return false;
  }

  bool exists(const GeometryComponent &component) const final
  {
    return component.attribute_domain_size(ATTR_DOMAIN_POINT) != 0;
  }
};

/**
 * In this function all the attribute providers for the instances component are created. Most data
 * in this function is statically allocated, because it does not change over time.
 */
static ComponentAttributeProviders create_attribute_providers_for_instances()
{
  static Float4x4AttributeProvider<get_matrix_position, set_matrix_position> position("position");
  static Float4x4AttributeProvider<get_matrix_rotation, set_matrix_rotation> rotation("rotation");
  static Float4x4AttributeProvider<get_matrix_scale, set_matrix_scale> scale("scale");
  return ComponentAttributeProviders({&position, &rotation, &scale}, {});
}

}  // namespace blender::bke

const blender::bke::ComponentAttributeProviders *InstancesComponent::get_attribute_providers()
    const
{
  static blender::bke::ComponentAttributeProviders providers =
      blender::bke::create_attribute_providers_for_instances();
  return &providers;
}

/** \} */
