#pragma once

#include <cstdint>
#include <cstring>
#include <vector>
#include <span>
#include <string>
#include <string_view>
#include <stack>

#include "Brezel/Reflection/Visitor.hpp"
#include "Brezel/Core/ComponentRegistry.hpp"
#include "Brezel/Core/EntityReference.hpp"
#include "Brezel/Core/UUID.hpp"
#include "Brezel/Foundation/StringID.hpp"

namespace Brezel {

/**
 * @brief Property filtering mode for binary component serialization.
 */
enum class BinaryPropertyFilter {
    All = 0,            ///< Serialize/deserialize all reflected properties
    NetworkedOnly = 1,  ///< Only visit properties annotated with Tag::Networked
    PersistentOnly = 2, ///< Only visit properties annotated with Tag::Persistent
};

namespace Detail {

inline bool matchesFilter(BinaryPropertyFilter filter, std::initializer_list<Tag> tags) noexcept {
    switch (filter) {
        case BinaryPropertyFilter::All:
            return true;
        case BinaryPropertyFilter::NetworkedOnly: {
            for (auto t : tags) if (t == Tag::Networked) return true;
            return false;
        }
        case BinaryPropertyFilter::PersistentOnly: {
            for (auto t : tags) if (t == Tag::Persistent) return true;
            return false;
        }
    }
    return true;
}

} // namespace Detail

/**
 * @brief High-speed binary serializer for reflected Brezel components.
 * Produces packed, deterministic byte sequences suitable for network synchronization or caching.
 */
class BinarySerializer : public ComponentVisitor {
public:
    explicit BinarySerializer(BinaryPropertyFilter filter = BinaryPropertyFilter::All)
        : filter_(filter) {}

    BinaryPropertyFilter getFilter() const noexcept { return filter_; }
    void setFilter(BinaryPropertyFilter filter) noexcept { filter_ = filter; }

    const std::vector<uint8_t>& getBuffer() const noexcept { return buffer_; }
    const uint8_t* data() const noexcept { return buffer_.data(); }
    size_t size() const noexcept { return buffer_.size(); }

    void clear() noexcept {
        buffer_.clear();
        while (!componentLengthOffsets_.empty()) componentLengthOffsets_.pop();
    }

    virtual bool beginComponent(StringID componentTypeName) override {
        if (ComponentRegistry::componentInfoByTypeName.contains(componentTypeName)) {
            const auto& info = ComponentRegistry::componentInfoByTypeName.at(componentTypeName);
            if (filter_ == BinaryPropertyFilter::PersistentOnly && ComponentRegistry::isTransient(info.flags)) {
                return false;
            }
            if (filter_ == BinaryPropertyFilter::NetworkedOnly && !ComponentRegistry::isNetworked(info.flags)) {
                return false;
            }
        }

        uint64_t hash = componentTypeName.hash();
        writeRaw(hash);

        // Reserve 4 bytes for payload length (back-patched in endComponent)
        size_t lenOffset = buffer_.size();
        uint32_t dummyLength = 0;
        writeRaw(dummyLength);
        componentLengthOffsets_.push(lenOffset);
        return true;
    }

    virtual void endComponent() override {
        if (componentLengthOffsets_.empty()) return;
        size_t lenOffset = componentLengthOffsets_.top();
        componentLengthOffsets_.pop();

        size_t payloadStart = lenOffset + sizeof(uint32_t);
        size_t payloadLen = buffer_.size() - payloadStart;
        uint32_t lenU32 = static_cast<uint32_t>(payloadLen);
        std::memcpy(&buffer_[lenOffset], &lenU32, sizeof(uint32_t));
    }

    virtual bool beginList(StringID, size_t) override { return true; }
    virtual void endList() override {}

    virtual void visit_property(StringID, int& val, std::initializer_list<Tag> tags) override {
        if (!Detail::matchesFilter(filter_, tags)) return;
        int32_t v = static_cast<int32_t>(val);
        writeRaw(v);
    }

    virtual void visit_property(StringID, float& val, std::initializer_list<Tag> tags) override {
        if (!Detail::matchesFilter(filter_, tags)) return;
        writeRaw(val);
    }

    virtual void visit_property(StringID, bool& val, std::initializer_list<Tag> tags) override {
        if (!Detail::matchesFilter(filter_, tags)) return;
        uint8_t v = val ? 1 : 0;
        writeRaw(v);
    }

    virtual void visit_property(StringID, std::string& str, std::initializer_list<Tag> tags) override {
        if (!Detail::matchesFilter(filter_, tags)) return;
        uint32_t len = static_cast<uint32_t>(str.size());
        writeRaw(len);
        if (len > 0) {
            writeBytes(str.data(), len);
        }
    }

    virtual void visit_property(StringID, StringID& sid, std::initializer_list<Tag> tags) override {
        if (!Detail::matchesFilter(filter_, tags)) return;
        uint64_t hash = sid.hash();
        writeRaw(hash);
    }

    virtual void visit_property(StringID, EntityReference& ref, std::initializer_list<Tag> tags) override {
        if (!Detail::matchesFilter(filter_, tags)) return;
        uint64_t uuidVal = ref.uuid.value;
        writeRaw(uuidVal);
    }

    virtual void visit_property(StringID label, VectorAccessorBase& va, std::initializer_list<Tag> tags) override {
        if (!Detail::matchesFilter(filter_, tags)) return;
        uint32_t count = static_cast<uint32_t>(va.size());
        writeRaw(count);
        for (size_t i = 0; i < va.size(); ++i) {
            va.visit_element(i, label, *this, tags);
        }
    }

    template <typename T>
    void writeRaw(const T& val) {
        static_assert(std::is_trivially_copyable_v<T>, "T must be trivially copyable for writeRaw");
        const auto* ptr = reinterpret_cast<const uint8_t*>(&val);
        buffer_.insert(buffer_.end(), ptr, ptr + sizeof(T));
    }

    void writeBytes(const void* src, size_t count) {
        if (!src || count == 0) return;
        const auto* ptr = static_cast<const uint8_t*>(src);
        buffer_.insert(buffer_.end(), ptr, ptr + count);
    }

private:
    BinaryPropertyFilter filter_{BinaryPropertyFilter::All};
    std::vector<uint8_t> buffer_{};
    std::stack<size_t> componentLengthOffsets_{};
};

/**
 * @brief High-speed binary deserializer for reflected Brezel components.
 * Reads packed byte buffers produced by BinarySerializer with strict bounds checking.
 */
class BinaryDeserializer : public ComponentVisitor {
public:
    BinaryDeserializer(const void* data, size_t size, BinaryPropertyFilter filter = BinaryPropertyFilter::All)
        : data_(static_cast<const uint8_t*>(data)), size_(size), filter_(filter) {}

    explicit BinaryDeserializer(std::span<const uint8_t> span, BinaryPropertyFilter filter = BinaryPropertyFilter::All)
        : data_(span.data()), size_(span.size()), filter_(filter) {}

    bool hasError() const noexcept { return error_; }
    bool isValid() const noexcept { return !error_; }
    size_t bytesRead() const noexcept { return cursor_; }
    size_t bytesRemaining() const noexcept { return (cursor_ < size_) ? (size_ - cursor_) : 0; }

    virtual bool beginComponent(StringID componentTypeName) override {
        if (error_) return false;

        uint64_t expectedHash = componentTypeName.hash();
        uint64_t wireHash = 0;
        if (!readRaw(wireHash)) {
            error_ = true;
            return false;
        }

        uint32_t payloadLen = 0;
        if (!readRaw(payloadLen)) {
            error_ = true;
            return false;
        }

        if (wireHash != expectedHash) {
            // Mismatched component type: skip payload bytes cleanly
            if (cursor_ + payloadLen <= size_) {
                cursor_ += payloadLen;
            } else {
                error_ = true;
            }
            return false;
        }

        return true;
    }

    virtual void endComponent() override {}
    virtual bool beginList(StringID, size_t) override { return true; }
    virtual void endList() override {}

    virtual void visit_property(StringID, int& val, std::initializer_list<Tag> tags) override {
        if (!Detail::matchesFilter(filter_, tags)) return;
        int32_t v = 0;
        if (readRaw(v)) {
            val = static_cast<int>(v);
        } else {
            error_ = true;
        }
    }

    virtual void visit_property(StringID, float& val, std::initializer_list<Tag> tags) override {
        if (!Detail::matchesFilter(filter_, tags)) return;
        if (!readRaw(val)) {
            error_ = true;
        }
    }

    virtual void visit_property(StringID, bool& val, std::initializer_list<Tag> tags) override {
        if (!Detail::matchesFilter(filter_, tags)) return;
        uint8_t v = 0;
        if (readRaw(v)) {
            val = (v != 0);
        } else {
            error_ = true;
        }
    }

    virtual void visit_property(StringID, std::string& str, std::initializer_list<Tag> tags) override {
        if (!Detail::matchesFilter(filter_, tags)) return;
        uint32_t len = 0;
        if (!readRaw(len)) {
            error_ = true;
            return;
        }
        if (len > bytesRemaining()) {
            error_ = true;
            return;
        }
        str.resize(len);
        if (len > 0) {
            readBytes(str.data(), len);
        }
    }

    virtual void visit_property(StringID, StringID& sid, std::initializer_list<Tag> tags) override {
        if (!Detail::matchesFilter(filter_, tags)) return;
        uint64_t hash = 0;
        if (readRaw(hash)) {
            sid = StringID(hash);
        } else {
            error_ = true;
        }
    }

    virtual void visit_property(StringID, EntityReference& ref, std::initializer_list<Tag> tags) override {
        if (!Detail::matchesFilter(filter_, tags)) return;
        uint64_t uuidVal = 0;
        if (readRaw(uuidVal)) {
            ref.uuid = UUID(uuidVal);
        } else {
            error_ = true;
        }
    }

    virtual void visit_property(StringID label, VectorAccessorBase& va, std::initializer_list<Tag> tags) override {
        if (!Detail::matchesFilter(filter_, tags)) return;
        uint32_t count = 0;
        if (!readRaw(count)) {
            error_ = true;
            return;
        }
        va.resize(count);
        for (size_t i = 0; i < count; ++i) {
            va.visit_element(i, label, *this, tags);
            if (error_) break;
        }
    }

    template <typename T>
    bool readRaw(T& outVal) {
        static_assert(std::is_trivially_copyable_v<T>, "T must be trivially copyable for readRaw");
        if (cursor_ + sizeof(T) > size_) {
            error_ = true;
            return false;
        }
        std::memcpy(&outVal, &data_[cursor_], sizeof(T));
        cursor_ += sizeof(T);
        return true;
    }

    bool readBytes(void* dest, size_t count) {
        if (count == 0) return true;
        if (!dest || cursor_ + count > size_) {
            error_ = true;
            return false;
        }
        std::memcpy(dest, &data_[cursor_], count);
        cursor_ += count;
        return true;
    }

private:
    const uint8_t* data_{nullptr};
    size_t size_{0};
    size_t cursor_{0};
    bool error_{false};
    BinaryPropertyFilter filter_{BinaryPropertyFilter::All};
};

} // namespace Brezel
