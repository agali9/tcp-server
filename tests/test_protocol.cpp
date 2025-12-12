#include "protocol.hpp"

#include <iostream>
#include <string>

int main() {
    const auto frame = tds::encode_message(tds::MessageType::Echo, std::string_view("hello"));

    tds::FrameParseResult parsed = tds::try_parse_frame(frame);
    if (!parsed.complete || parsed.message.type != tds::MessageType::Echo) {
        std::cerr << "single frame parse failed\n";
        return 1;
    }

    std::vector<std::uint8_t> stream = frame;
    const auto frame2 = tds::encode_message(tds::MessageType::Ping, std::string_view(""));
    stream.insert(stream.end(), frame2.begin(), frame2.end());

    parsed = tds::try_parse_frame(stream);
    if (!parsed.complete) {
        std::cerr << "coalesced first frame failed\n";
        return 1;
    }
    stream.erase(stream.begin(), stream.begin() + static_cast<std::ptrdiff_t>(parsed.bytes_consumed));

    parsed = tds::try_parse_frame(stream);
    if (!parsed.complete || parsed.message.type != tds::MessageType::Ping) {
        std::cerr << "coalesced second frame failed\n";
        return 1;
    }

    const auto put_payload = tds::encode_put_request("user:42", "alice");
    const auto kv = tds::decode_kv_put(
        std::string_view(reinterpret_cast<const char*>(put_payload.data()), put_payload.size()));
    if (!kv || kv->first != "user:42" || kv->second != "alice") {
        std::cerr << "kv put decode failed\n";
        return 1;
    }

    std::cout << "test_protocol ok\n";
    return 0;
}
