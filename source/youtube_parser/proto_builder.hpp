#ifndef PROTO_BUILDER_HPP
#define PROTO_BUILDER_HPP

#include <vector>
#include <string>
#include <stdexcept>
#include <cstdint>
#include <ctime>
#include <curl/curl.h>

class ProtoBuilder {
  public:
	ProtoBuilder();
	std::vector<uint8_t> toBytes() const;
	std::string toUrlencodedBase64() const;
	void varint(int field, uint64_t val);
	void string(int field, const std::string &str);
	void bytes(int field, const std::vector<uint8_t> &bytes);

  private:
	std::vector<uint8_t> byteBuffer;
	void writeVarint(uint64_t val);
	void field_(int field, uint8_t wire);
	std::string base64UrlEncode(const std::vector<uint8_t> &data) const;
	std::string urlEncode(const std::string &str) const;
};

#endif