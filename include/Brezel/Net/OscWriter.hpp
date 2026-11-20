#pragma once

#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <string_view>

namespace Brezel::Net {

/**
 * @brief OSC 1.0 encoder into a caller's buffer: one message, or one bundle of messages, per packet.
 *
 * No allocation, no exception, no system call, no dependency beyond the standard headers above. The writer never
 * writes past the end of the buffer it was given and never keeps a pointer to anything the caller passes in.
 *
 * Encoding (OSC 1.0): the address and every string are written with a terminating NUL and padded with zeros to a
 * multiple of 4 bytes; the type tag string is written with its leading ',' the same way; int32, int64, float32,
 * float64 and the bundle time tag are big-endian; a 'T' or 'F' tag takes no bytes in the arguments. A bundle is
 * "#bundle\0", an 8-byte time tag (1 means "immediately"), then each message preceded by its size as a big-endian
 * int32.
 *
 * Call order:
 * - a packet that is one message: beginMessage(), one add call per type tag in order, endMessage();
 * - a packet that is a bundle: beginBundle(), any number of messages as above, endBundle().
 * Anything else is an error: a second top-level message or bundle in the same packet, a nested bundle, an add call
 * whose type does not match the next tag, too few or too many add calls, an address that does not start with '/',
 * an address, tag or string holding a NUL, a tag outside the subset below, endBundle() without a bundle or inside
 * a message.
 *
 * Errors: any error (overflow included) sets a sticky error flag; every later call returns false until reset() or a
 * successful rewind(). A failed call writes nothing, and the packet is cut back to the last complete message (the
 * start of the open message, or of its bundle element), so size() and data() only ever cover complete messages
 * (plus the bundle header when a bundle is open). overflowed() tells an overflow apart from a misuse.
 *
 * mark() and rewind(): a mark is the current size, taken between messages. rewind(mark) cuts the packet back to it,
 * closes any open message, and clears the error flags: a sender that splits bundles at a datagram size writes a
 * message, finds the packet too long, rewinds to the mark taken before it, ends and sends the bundle, and starts the
 * next one with that message. A mark before an open bundle's header closes the bundle; a mark inside the header, or
 * past size(), is refused (false, no error set).
 *
 * Subset of OSC 1.0 written: int32 'i', int64 'h', float32 'f', float64 'd', string 's', true 'T', false 'F';
 * bundles one level deep. Not written: blobs 'b', MIDI 'm', time tags as arguments 't', symbols, chars, colours,
 * nil, infinitum, arrays, nested bundles.
 *
 * Threads: none of its own; one writer object is used by one thread at a time.
 */
class OscWriter {
public:
    explicit OscWriter(std::span<std::byte> buffer) noexcept : buffer_(buffer) {}

    /// Empty the packet and clear every flag.
    void reset() noexcept {
        size_ = 0;
        error_ = false;
        overflow_ = false;
        inBundle_ = false;
        inMessage_ = false;
        bundleStart_ = 0;
        messageStart_ = 0;
        elementSizeAt_ = noElement;
        tagsAt_ = 0;
        tagCount_ = 0;
        nextTag_ = 0;
    }

    /// "#bundle\0" and the time tag (1 = immediately). Only at the start of a packet; bundles do not nest.
    bool beginBundle(uint64_t timetag = 1) noexcept {
        if (error_) return false;
        if (inBundle_ || inMessage_ || size_ != 0) return fail();
        if (!fits(bundleHeaderSize)) return failOverflow();
        static constexpr char tag[8] = {'#', 'b', 'u', 'n', 'd', 'l', 'e', '\0'};
        std::memcpy(buffer_.data() + size_, tag, 8);
        putU64(size_ + 8, timetag);
        bundleStart_ = size_;
        size_ += bundleHeaderSize;
        inBundle_ = true;
        return true;
    }

    /// Close the open bundle (writes no bytes). Not inside a message.
    bool endBundle() noexcept {
        if (error_) return false;
        if (!inBundle_ || inMessage_) return fail();
        inBundle_ = false;
        return true;
    }

    /// The address (starts with '/') and the type tags without the leading ',' (e.g. "ff", "siT"); the add calls
    /// must follow the tags in order. In a bundle, the element's size is written by endMessage().
    bool beginMessage(std::string_view address, std::string_view typeTags) noexcept {
        if (error_) return false;
        if (inMessage_ || (!inBundle_ && size_ != 0)) return fail();
        if (address.empty() || address.front() != '/' || hasNul(address)) return fail();
        for (char t : typeTags) {
            if (!isSupportedTag(t)) return fail();
        }
        const size_t elementHeader = inBundle_ ? 4 : 0;
        const size_t addressBytes = paddedSize(address.size());
        const size_t tagBytes = paddedSize(typeTags.size() + 1);
        if (!fits(elementHeader + addressBytes + tagBytes)) return failOverflow();

        messageStart_ = size_;
        size_t at = size_;
        if (inBundle_) {
            elementSizeAt_ = at;
            putU32(at, 0);  // patched by endMessage()
            at += 4;
        } else {
            elementSizeAt_ = noElement;
        }
        at = putPadded(at, address);
        buffer_[at] = std::byte{','};
        tagsAt_ = at + 1;
        at = putPaddedAfterComma(at, typeTags);
        tagCount_ = typeTags.size();
        nextTag_ = 0;
        size_ = at;
        inMessage_ = true;
        return true;
    }

    bool addInt32(int32_t v) noexcept {  // 'i'
        if (!takeTag('i', 4)) return false;
        putU32(size_, static_cast<uint32_t>(v));
        size_ += 4;
        return true;
    }

    bool addInt64(int64_t v) noexcept {  // 'h'
        if (!takeTag('h', 8)) return false;
        putU64(size_, static_cast<uint64_t>(v));
        size_ += 8;
        return true;
    }

    bool addFloat(float v) noexcept {  // 'f'
        if (!takeTag('f', 4)) return false;
        putU32(size_, std::bit_cast<uint32_t>(v));
        size_ += 4;
        return true;
    }

    bool addDouble(double v) noexcept {  // 'd'
        if (!takeTag('d', 8)) return false;
        putU64(size_, std::bit_cast<uint64_t>(v));
        size_ += 8;
        return true;
    }

    bool addString(std::string_view v) noexcept {  // 's'
        if (error_) return false;
        if (hasNul(v)) return failInMessage();
        if (!takeTag('s', paddedSize(v.size()))) return false;
        size_ = putPadded(size_, v);
        return true;
    }

    /// 'T' or 'F': the value must match the tag; writes no bytes.
    bool addBool(bool v) noexcept { return takeTag(v ? 'T' : 'F', 0); }

    /// Every tag consumed; in a bundle, writes the element's size.
    bool endMessage() noexcept {
        if (error_) return false;
        if (!inMessage_ || nextTag_ != tagCount_) return failInMessage();
        if (elementSizeAt_ != noElement) putU32(elementSizeAt_, static_cast<uint32_t>(size_ - elementSizeAt_ - 4));
        inMessage_ = false;
        elementSizeAt_ = noElement;
        return true;
    }

    size_t size() const noexcept { return size_; }
    std::span<const std::byte> data() const noexcept { return {buffer_.data(), size_}; }
    size_t capacity() const noexcept { return buffer_.size(); }
    bool hasError() const noexcept { return error_; }
    bool overflowed() const noexcept { return overflow_; }
    bool inBundle() const noexcept { return inBundle_; }
    bool inMessage() const noexcept { return inMessage_; }

    /// The current size, to rewind to; take it between messages.
    size_t mark() const noexcept { return size_; }

    /// Cut the packet back to a mark, close any open message and clear the error flags (see the class comment).
    bool rewind(size_t mark) noexcept {
        if (mark > size_) return false;
        if (inBundle_ && mark > bundleStart_ && mark < bundleStart_ + bundleHeaderSize) return false;
        if (inBundle_ && mark <= bundleStart_) inBundle_ = false;
        size_ = mark;
        inMessage_ = false;
        elementSizeAt_ = noElement;
        tagCount_ = 0;
        nextTag_ = 0;
        error_ = false;
        overflow_ = false;
        return true;
    }

private:
    static constexpr size_t bundleHeaderSize = 16;
    static constexpr size_t noElement = static_cast<size_t>(-1);

    static constexpr size_t paddedSize(size_t textLength) noexcept { return (textLength + 4) & ~size_t{3}; }

    static constexpr bool isSupportedTag(char t) noexcept {
        return t == 'i' || t == 'h' || t == 'f' || t == 'd' || t == 's' || t == 'T' || t == 'F';
    }

    static bool hasNul(std::string_view s) noexcept { return s.find('\0') != std::string_view::npos; }

    bool fits(size_t bytes) const noexcept { return bytes <= buffer_.size() - size_; }

    // Cut back to the last complete message: the start of the open message (its bundle element included).
    void cutBack() noexcept {
        if (inMessage_) size_ = messageStart_;
        inMessage_ = false;
        elementSizeAt_ = noElement;
    }

    bool fail() noexcept {
        error_ = true;
        return false;
    }

    bool failInMessage() noexcept {
        cutBack();
        return fail();
    }

    bool failOverflow() noexcept {
        overflow_ = true;
        return failInMessage();
    }

    // Checks that the next tag is `tag` and that `bytes` more fit; on success the tag is consumed.
    bool takeTag(char tag, size_t bytes) noexcept {
        if (error_) return false;
        if (!inMessage_ || nextTag_ >= tagCount_) return failInMessage();
        if (static_cast<char>(buffer_[tagsAt_ + nextTag_]) != tag) return failInMessage();
        if (!fits(bytes)) return failOverflow();
        ++nextTag_;
        return true;
    }

    void putU32(size_t at, uint32_t v) noexcept {
        for (size_t i = 0; i < 4; ++i) buffer_[at + i] = static_cast<std::byte>(v >> (24 - 8 * i));
    }

    void putU64(size_t at, uint64_t v) noexcept {
        for (size_t i = 0; i < 8; ++i) buffer_[at + i] = static_cast<std::byte>(v >> (56 - 8 * i));
    }

    // Writes text, its NUL and the zero padding at `at`; returns the offset after it. The caller checked the room.
    size_t putPadded(size_t at, std::string_view text) noexcept {
        const size_t total = paddedSize(text.size());
        if (!text.empty()) std::memcpy(buffer_.data() + at, text.data(), text.size());
        std::memset(buffer_.data() + at + text.size(), 0, total - text.size());
        return at + total;
    }

    // The type tag string: the ',' already sits at `commaAt`; writes the tags, the NUL and the padding.
    size_t putPaddedAfterComma(size_t commaAt, std::string_view tags) noexcept {
        const size_t total = paddedSize(tags.size() + 1);
        if (!tags.empty()) std::memcpy(buffer_.data() + commaAt + 1, tags.data(), tags.size());
        std::memset(buffer_.data() + commaAt + 1 + tags.size(), 0, total - 1 - tags.size());
        return commaAt + total;
    }

    std::span<std::byte> buffer_;
    size_t size_ = 0;
    bool error_ = false;
    bool overflow_ = false;
    bool inBundle_ = false;
    bool inMessage_ = false;
    size_t bundleStart_ = 0;
    size_t messageStart_ = 0;
    size_t elementSizeAt_ = noElement;
    size_t tagsAt_ = 0;
    size_t tagCount_ = 0;
    size_t nextTag_ = 0;
};

}  // namespace Brezel::Net
