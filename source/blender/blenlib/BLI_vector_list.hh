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

#pragma once

#include <algorithm>

#include "BLI_vector.hh"

namespace blender {

template<typename T> class VectorList {
 private:
  using UsedVector = Vector<T, 0>;
  static constexpr int64_t vector_capacity_start = 32;
  static constexpr int64_t vector_capacity_soft_limit = 4096;

  /**
   * Contains the individual vectors. There must always be at least one vector.
   */
  Vector<UsedVector> vectors_;

 public:
  VectorList()
  {
    this->append_vector();
  }

  void append(const T &value)
  {
    this->append_as(value);
  }

  void append(T &&value)
  {
    this->append_as(std::move(value));
  }

  template<typename ForwardT> void append_as(ForwardT &&value)
  {
    UsedVector &vector = this->ensure_space_for_one();
    vector.append_unchecked_as(std::forward<ForwardT>(value));
  }

  UsedVector *begin()
  {
    return vectors_.begin();
  }

  UsedVector *end()
  {
    return vectors_.end();
  }

  const UsedVector *begin() const
  {
    return vectors_.begin();
  }

  const UsedVector *end() const
  {
    return vectors_.end();
  }

 private:
  UsedVector &ensure_space_for_one()
  {
    UsedVector &vector = vectors_.last();
    if (LIKELY(!vector.is_at_capacity())) {
      return vector;
    }
    this->append_vector();
    return vectors_.last();
  }

  void append_vector()
  {
    const int64_t new_vector_capacity = this->get_next_vector_capacity();
    vectors_.append({});
    vectors_.last().reserve(new_vector_capacity);
  }

  int64_t get_next_vector_capacity()
  {
    if (vectors_.is_empty()) {
      return vector_capacity_start;
    }
    return std::min(vectors_.last().capacity() * 2, vector_capacity_soft_limit);
  }
};

}  // namespace blender