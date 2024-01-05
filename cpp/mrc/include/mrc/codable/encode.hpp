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
#include "mrc/codable/codable_protocol.hpp"
#include "mrc/codable/encoded_object_proto.hpp"
#include "mrc/codable/memory.hpp"
#include "mrc/codable/type_traits.hpp"
#include "mrc/codable/types.hpp"
#include "mrc/memory/memory_block_provider.hpp"
#include "mrc/utils/sfinae_concept.hpp"

#include <cstdint>
#include <memory>
#include <type_traits>

namespace mrc::codable {

template <typename T>
class Encoder final
{
  public:
    Encoder(IEncodableStorage& storage) : m_storage(storage) {}

    void serialize(const T& obj, const EncodingOptions& opts = {})
    {
        auto parent = m_storage.push_context(typeid(T));
        detail::serialize(sfinae::full_concept{}, obj, *this, opts);
        m_storage.pop_context(parent);
    }

  protected:
    std::optional<idx_t> register_memory_view(memory::const_buffer_view view, bool force_register = false)
    {
        return m_storage.register_memory_view(std::move(view), force_register);
    }

    idx_t copy_to_eager_descriptor(memory::const_buffer_view view)
    {
        return m_storage.copy_to_eager_descriptor(std::move(view));
    }

    idx_t add_meta_data(const google::protobuf::Message& meta_data)
    {
        return m_storage.add_meta_data(meta_data);
    }

    idx_t create_memory_buffer(std::size_t bytes)
    {
        return m_storage.create_memory_buffer(bytes);
    }

    void copy_to_buffer(idx_t buffer_idx, memory::const_buffer_view view)
    {
        m_storage.copy_to_buffer(buffer_idx, std::move(view));
    }

    template <typename U>
    Encoder<U> rebind()
    {
        return Encoder<U>(m_storage);
    }

    IEncodableStorage& storage()
    {
        return m_storage;
    }

  private:
    IEncodableStorage& m_storage;

    friend T;
    friend codable_protocol<T>;
};

class EncoderBase
{
  public:
    //  Public constructor is necessary here to allow using statement. Not really a concern since the object isnt very
    //  useful in the base class
    EncoderBase(LocalSerializedWrapper& encoded_object, memory::memory_block_provider& block_provider);

  protected:
    size_t write_descriptor(memory::const_buffer_view view, DescriptorKind kind);

    // std::optional<idx_t> register_memory_view(memory::const_buffer_view view, bool force_register = false)
    // {
    //     // return m_storage.register_memory_view(std::move(view), force_register);
    //     return -1;
    // }

    // idx_t copy_to_eager_descriptor(memory::const_buffer_view view)
    // {
    //     return m_encoded_object.add_eager_descriptor(view);
    // }

    idx_t add_meta_data(const google::protobuf::Message& meta_data)
    {
        // return m_storage.add_meta_data(meta_data);
        return -1;
    }

    // idx_t create_memory_buffer(std::size_t bytes)
    // {
    //     // return m_storage.create_memory_buffer(bytes);
    //     return -1;
    // }

    // void copy_to_buffer(idx_t buffer_idx, memory::const_buffer_view view)
    // {
    //     // m_storage.copy_to_buffer(buffer_idx, std::move(view));
    // }

    LocalSerializedWrapper& m_encoded_object;
    memory::memory_block_provider& m_block_provider;
};

template <typename T>
class Encoder2 final : public EncoderBase
{
  public:
    using EncoderBase::EncoderBase;

  private:
    void serialize2(const T& obj, const EncodingOptions& opts = {})
    {
        auto obj_idx = m_encoded_object.push_current_object_idx(typeid(T));
        detail::serialize2(obj, *this, opts);
        m_encoded_object.pop_current_object_idx(obj_idx);
    }

    template <typename U>
    Encoder2<U> rebind()
    {
        return Encoder2<U>(m_encoded_object, m_block_provider);
    }

    friend T;
    friend codable_protocol<T>;

    template <typename U, typename V>
    friend void encode2(const U& obj, Encoder2<V>& encoder, EncodingOptions opts);
};

template <typename T>
void encode(const T& obj, IEncodableStorage& storage, EncodingOptions opts = {})
{
    Encoder<T> encoder(storage);
    encoder.serialize(obj, std::move(opts));
}

template <typename T>
void encode(const T& obj, IEncodableStorage* storage, EncodingOptions opts = {})
{
    Encoder<T> encoder(*storage);
    encoder.serialize(obj, std::move(opts));
}

// This method for nested calls to encode2
template <typename T, typename U>
void encode2(const T& obj, Encoder2<U>& encoder, EncodingOptions opts = {})
{
    static_assert(is_encodable_v<T>, "Must use an encodable object");

    if constexpr (std::is_same_v<T, U>)
    {
        encoder.serialize2(obj, std::move(opts));
    }
    else
    {
        // Rebind the type
        encoder.template rebind<T>().serialize2(obj, std::move(opts));
    }
}

// This method is used for top level calls to encode
template <typename T>
std::unique_ptr<LocalSerializedWrapper> encode2(const T& obj,
                                                std::shared_ptr<memory::memory_block_provider> block_provider,
                                                EncodingOptions opts = {})
{
    auto encoded_object = std::make_unique<LocalSerializedWrapper>();

    Encoder2<T> encoder(*encoded_object, *block_provider);
    encode2(obj, encoder, std::move(opts));

    auto encoded_string = encoded_object->proto().DebugString();

    VLOG(10) << "Encoded object proto: \n" << encoded_string;

    return encoded_object;
}

}  // namespace mrc::codable
