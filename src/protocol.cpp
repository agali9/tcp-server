#include "protocol.hpp"

#include <arpa/inet.h>

#include <cstring>
#include <stdexcept>

namespace tds {

std::uint32_t host_to_network_u32(std::uint32_t value) {
    return htonl(value);
}

std::uint32_t network_to_host_u32(std::uint32_t value) {
    return ntohl(value);
}

std::vector<std::uint8_t> encode_message(MessageType type, std::string_view payload) {
    if (payload.size() > UINT32_MAX) {
        throw std::length_error("payload too large");
    }

    std::vector<std::uint8_t> frame;
    frame.resize(kHeaderSize + payload.size());

    const std::uint32_t magic_net = host_to_network_u32(kMagic);
    const std::uint32_t len_net = host_to_network_u32(static_cast<std::uint32_t>(payload.size()));

    std::memcpy(frame.data(), &magic_net, sizeof(magic_net));
    frame[4] = kProtocolVersion;
    frame[5] = static_cast<std::uint8_t>(type);
    std::memcpy(frame.data() + 6, &len_net, sizeof(len_net));
    if (!payload.empty()) {
        std::memcpy(frame.data() + kHeaderSize, payload.data(), payload.size());
    }
    return frame;
}

std::vector<std::uint8_t> encode_message(MessageType type, const std::vector<std::uint8_t>& payload) {
    return encode_message(type, std::string_view(reinterpret_cast<const char*>(payload.data()), payload.size()));
}

FrameParseResult try_parse_frame(const std::vector<std::uint8_t>& buffer) {
    FrameParseResult result;
    if (buffer.size() < kHeaderSize) {
        return result;
    }

    std::uint32_t magic_net = 0;
    std::memcpy(&magic_net, buffer.data(), sizeof(magic_net));
    const std::uint32_t magic = network_to_host_u32(magic_net);
    if (magic != kMagic) {
        throw std::runtime_error("invalid protocol magic");
    }

    if (buffer[4] != kProtocolVersion) {
        throw std::runtime_error("unsupported protocol version");
    }

    std::uint32_t len_net = 0;
    std::memcpy(&len_net, buffer.data() + 6, sizeof(len_net));
    const std::uint32_t payload_len = network_to_host_u32(len_net);
    const std::size_t frame_size = kHeaderSize + payload_len;

    if (buffer.size() < frame_size) {
        return result;  // incomplete frame
    }

    result.complete = true;
    result.bytes_consumed = frame_size;
    result.message.type = static_cast<MessageType>(buffer[5]);
    result.message.payload.assign(buffer.begin() + kHeaderSize, buffer.begin() + frame_size);
    return result;
}

static void append_u16_be(std::vector<std::uint8_t>& out, std::uint16_t value) {
    out.push_back(static_cast<std::uint8_t>((value >> 8) & 0xFF));
    out.push_back(static_cast<std::uint8_t>(value & 0xFF));
}

static std::optional<std::uint16_t> read_u16_be(std::string_view data, std::size_t& offset) {
    if (offset + 2 > data.size()) {
        return std::nullopt;
    }
    const std::uint16_t value = (static_cast<std::uint8_t>(data[offset]) << 8) |
                                  static_cast<std::uint8_t>(data[offset + 1]);
    offset += 2;
    return value;
}

std::vector<std::uint8_t> encode_get_request(std::string_view key) {
    if (key.size() > UINT16_MAX) {
        throw std::length_error("key too long");
    }
    std::vector<std::uint8_t> payload;
    append_u16_be(payload, static_cast<std::uint16_t>(key.size()));
    payload.insert(payload.end(), key.begin(), key.end());
    return payload;
}

std::vector<std::uint8_t> encode_put_request(std::string_view key, std::string_view value) {
    if (key.size() > UINT16_MAX || value.size() > UINT16_MAX) {
        throw std::length_error("key or value too long");
    }
    std::vector<std::uint8_t> payload;
    append_u16_be(payload, static_cast<std::uint16_t>(key.size()));
    payload.insert(payload.end(), key.begin(), key.end());
    append_u16_be(payload, static_cast<std::uint16_t>(value.size()));
    payload.insert(payload.end(), value.begin(), value.end());
    return payload;
}

std::optional<std::string> decode_kv_key(std::string_view payload) {
    std::size_t offset = 0;
    const auto key_len = read_u16_be(payload, offset);
    if (!key_len || offset + *key_len > payload.size()) {
        return std::nullopt;
    }
    return std::string(payload.substr(offset, *key_len));
}

std::optional<std::pair<std::string, std::string>> decode_kv_put(std::string_view payload) {
    std::size_t offset = 0;
    const auto key_len = read_u16_be(payload, offset);
    if (!key_len || offset + *key_len > payload.size()) {
        return std::nullopt;
    }
    const std::string key(payload.substr(offset, *key_len));
    offset += *key_len;

    const auto val_len = read_u16_be(payload, offset);
    if (!val_len || offset + *val_len > payload.size()) {
        return std::nullopt;
    }
    const std::string value(payload.substr(offset, *val_len));
    return std::make_pair(key, value);
}

}  // namespace tds
