#include "proto_builder.hpp"

ProtoBuilder::ProtoBuilder() = default;

std::vector<uint8_t> ProtoBuilder::toBytes() const {
    return byteBuffer;
}

std::string ProtoBuilder::toUrlencodedBase64() const {
    std::string b64 = base64UrlEncode(byteBuffer);
    return urlEncode(b64);
}

void ProtoBuilder::varint(int field, uint64_t val) {
    field_(field, 0);
    writeVarint(val);
}

void ProtoBuilder::string(int field, const std::string &str) {
    bytes(field, std::vector<uint8_t>(str.begin(), str.end()));
}

void ProtoBuilder::bytes(int field, const std::vector<uint8_t> &bytes) {
    field_(field, 2);
    writeVarint(bytes.size());
    byteBuffer.insert(byteBuffer.end(), bytes.begin(), bytes.end());
}

void ProtoBuilder::writeVarint(uint64_t val) {
    while (val > 0x7F) {
        byteBuffer.push_back(static_cast<uint8_t>((val & 0x7F) | 0x80));
        val >>= 7;
    }
    byteBuffer.push_back(static_cast<uint8_t>(val & 0x7F));
}

void ProtoBuilder::field_(int field, uint8_t wire) {
    uint64_t val = (static_cast<uint64_t>(field) << 3) | (wire & 0x07);
    writeVarint(val);
}

std::string ProtoBuilder::base64UrlEncode(const std::vector<uint8_t> &data) const {
    static const char *base64_chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
    std::string result;
    int val = 0, valb = -6;
    for (uint8_t c : data) {
        val = (val << 8) + c;
        valb += 8;
        while (valb >= 0) {
            result.push_back(base64_chars[(val >> valb) & 0x3F]);
            valb -= 6;
        }
    }
    if (valb > -6) result.push_back(base64_chars[((val << 8) >> (valb + 8)) & 0x3F]);
    return result;
}

std::string ProtoBuilder::urlEncode(const std::string &str) const {
    CURL *curl = curl_easy_init();
    if (!curl) return "";
    char *encoded = curl_easy_escape(curl, str.c_str(), str.size());
    std::string result(encoded);
    curl_free(encoded);
    curl_easy_cleanup(curl);
    return result;
}