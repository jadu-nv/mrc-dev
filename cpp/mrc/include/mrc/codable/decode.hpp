/*
 * SPDX-FileCopyrightText: Copyright (c) 2021-2023, NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once

#include "mrc/codable/api.hpp"
#include "mrc/codable/encoded_object_proto.hpp"
#include "mrc/codable/storage_forwarder.hpp"
#include "mrc/codable/type_traits.hpp"
#include "mrc/memory/memory_block_provider.hpp"
#include "mrc/protos/codable.pb.h"
#include "mrc/utils/sfinae_concept.hpp"

#include <glog/logging.h>

#include <cstring>
#include <memory>

namespace mrc::codable {

template <typename T>
struct Decoder final : public StorageForwarder
{
  public:
    Decoder(const IDecodableStorage& storage) : m_storage(storage) {}

    T deserialize(std::size_t object_idx) const
    {
        return detail::deserialize<T>(sfinae::full_concept{}, *this, object_idx);
    }

  protected:
    void copy_from_buffer(const idx_t& idx, memory::buffer_view dst_view) const
    {
        m_storage.copy_from_buffer(idx, std::move(dst_view));
    }

    std::size_t buffer_size(const idx_t& idx) const
    {
        return m_storage.buffer_size(idx);
    }

    std::shared_ptr<mrc::memory::memory_resource> host_memory_resource() const
    {
        return m_storage.host_memory_resource();
    }

    std::shared_ptr<mrc::memory::memory_resource> device_memory_resource() const
    {
        return m_storage.host_memory_resource();
    }

  private:
    const IStorage& const_storage() const final
    {
        return m_storage;
    }

    const IDecodableStorage& m_storage;

    friend T;
    friend codable_protocol<T>;
};

class DecoderBase
{
  public:
    //  Public constructor is necessary here to allow using statement. Not really a concern since the object isnt very
    //  useful in the base class
    DecoderBase(const DescriptorObjectHandler& encoded_object);

  protected:
    void read_descriptor(memory::buffer_view dst_view) const;

    std::size_t descriptor_size() const;

    // /**
    //  * @brief Hash of std::type_index for the object at idx
    //  *
    //  * @param object_idx
    //  * @return std::type_index
    //  */
    // std::size_t type_index_hash_for_object(const obj_idx_t& object_idx) const
    // {
    //     return m_encoded_object.type_index_hash_for_object(object_idx);
    // }

    // /**
    //  * @brief Starting index of object at idx
    //  *
    //  * @param object_idx
    //  * @return idx_t
    //  */
    // idx_t start_idx_for_object(const obj_idx_t& object_idx) const
    // {
    //     return m_encoded_object.start_idx_for_object(object_idx);
    // }

    // /**
    //  * @brief Parent for object at idx
    //  *
    //  * @return std::optional<obj_idx_t> - if nullopt, then the object is a top-level object; otherwise, it is a child
    //  * object with a parent object at the returned value
    //  */
    // std::optional<obj_idx_t> parent_obj_idx_for_object(const obj_idx_t& object_idx) const
    // {
    //     return m_encoded_object.parent_obj_idx_for_object(object_idx);
    // }

    // std::shared_ptr<mrc::memory::memory_resource> host_memory_resource() const
    // {
    //     return m_storage.host_memory_resource();
    // }

    // std::shared_ptr<mrc::memory::memory_resource> device_memory_resource() const
    // {
    //     return m_storage.host_memory_resource();
    // }

    const DescriptorObjectHandler& m_encoded_object;
};

template <typename T>
struct Decoder2 final : public DecoderBase
{
  public:
    using DecoderBase::DecoderBase;

  private:
    T deserialize() const
    {
        T value = detail::deserialize2<T>(*this);
        m_encoded_object.increment_payload_idx();
        return value;
    }

    template <typename U>
    Decoder2<U> rebind() const
    {
        auto decoder = Decoder2<U>(m_encoded_object);

        return decoder;
    }

    friend T;
    friend codable_protocol<T>;

    template <typename U, typename V>
    friend U decode2(const Decoder2<V>& encoder);
};

template <typename T>
auto decode(const IDecodableStorage& encoded, std::size_t object_idx = 0)
{
    Decoder<T> decoder(encoded);
    return decoder.deserialize(object_idx);
}

// This method for nested calls to decode2
template <typename T, typename U>
T decode2(const Decoder2<U>& decoder)
{
    static_assert(is_decodable_v<T>, "Must use an encodable object");

    if constexpr (std::is_same_v<T, U>)
    {
        return decoder.deserialize();
    }
    else
    {
        // Rebind the type
        return decoder.template rebind<T>().deserialize();
    }
}

template <typename T>
T decode2(const DescriptorObjectHandler& encoded)
{
    Decoder2<T> decoder(encoded);
    return decode2<T>(decoder);
}

}  // namespace mrc::codable
