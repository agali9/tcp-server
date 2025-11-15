#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace tds {

inline constexpr std::uint32_t kMagic = 0x54445053;  // "TDPS"
inline constexpr std::uint8_t kProtocolVersion = 1;
inline constexpr std::size_t kHeaderSize = 10;

enum class MessageType : std::uint8_t {
    Ping = 0x01,
    Pong = 0x02,
    Echo = 0x03,
    Get = 0x04,
    GetResponse = 0x05,
    Put = 0x06,
    PutResponse = 0x07,
    Stats = 0x08,
    StatsResponse = 0x09,
    Error = 0xFF,
};

enum class ErrorCode : std::uint8_t {
    None = 0,
    BadRequest = 1,
    NotFound = 2,
    Internal = 3,
};

struct Message {
    MessageType type{MessageType::Ping};
    std::vector<std::uint8_t> payload;
};

struct FrameParseResult {
    bool complete{false};
    std::size_t bytes_consumed{0};
    Message message;
};

// Serialize a message into a length-prefixed binary frame.
std::vector<std::uint8_t> encode_message(MessageType type, std::string_view payload);
std::vector<std::uint8_t> encode_message(MessageType type, const std::vector<std::uint8_t>& payload);

// Incrementally parse frames from a byte stream buffer.
FrameParseResult try_parse_frame(const std::vector<std::uint8_t>& buffer);

// Helpers for KV operations
std::vector<std::uint8_t> encode_get_request(std::string_view key);
std::vector<std::uint8_t> encode_put_request(std::string_view key, std::string_view value);
std::optional<std::string> decode_kv_key(std::string_view payload);
std::optional<std::pair<std::string, std::string>> decode_kv_put(std::string_view payload);

std::uint32_t host_to_network_u32(std::uint32_t value);
std::uint32_t network_to_host_u32(std::uint32_t value);

}  // namespace tds
