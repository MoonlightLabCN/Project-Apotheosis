#pragma once
#include <stdint.h>
#include <string.h>
#include <string>
#include <vector>

namespace DnsWire {
struct Answer { std::string addresses; unsigned ttl = 300; };
inline unsigned U16(const unsigned char* p) { return (p[0] << 8) | p[1]; }
inline unsigned U32(const unsigned char* p) { return (unsigned(p[0]) << 24) | (unsigned(p[1]) << 16) | (p[2] << 8) | p[3]; }
inline std::string Lower(std::string s) {
    for (char& c : s) if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
    if (!s.empty() && s.back() == '.') s.pop_back();
    return s;
}
inline bool Name(const unsigned char* data, size_t size, size_t& pos, std::string& out) {
    size_t at = pos; bool jumped = false; out.clear();
    for (unsigned steps = 0; steps < 128; ++steps) {
        if (at >= size) return false;
        unsigned n = data[at++];
        if ((n & 0xc0) == 0xc0) {
            if (at >= size) return false;
            unsigned target = ((n & 63) << 8) | data[at++];
            if (!jumped) pos = at;
            jumped = true; at = target; continue;
        }
        if (n & 0xc0 || n > 63 || n > size - at) return false;
        if (!n) { if (!jumped) pos = at; out = Lower(out); return true; }
        if (!out.empty()) out += '.';
        out.append(reinterpret_cast<const char*>(data + at), n); at += n;
        if (out.size() > 253) return false;
    }
    return false;
}
inline std::vector<unsigned char> Query(const std::string& host) {
    std::vector<unsigned char> q(12, 0); q[2] = 1; q[5] = 1;
    std::string name = Lower(host);
    if (name.empty() || name.size() > 253) return {};
    size_t at = 0;
    while (at < name.size()) {
        size_t end = name.find('.', at); if (end == std::string::npos) end = name.size();
        size_t n = end - at; if (!n || n > 63) return {};
        q.push_back(static_cast<unsigned char>(n));
        q.insert(q.end(), name.begin() + at, name.begin() + end); at = end + 1;
    }
    q.insert(q.end(), {0, 0, 1, 0, 1}); return q;
}
inline bool Parse(const unsigned char* data, size_t size, const std::string& host, Answer& result) {
    if (size < 12 || U16(data) != 0 || !(data[2] & 0x80) || (data[2] & 0x7a) ||
        (data[3] & 15) || U16(data + 4) != 1 || U16(data + 6) > 128) return false;
    size_t pos = 12; std::string name;
    if (!Name(data, size, pos, name) || name != Lower(host) || size - pos < 4 ||
        U16(data + pos) != 1 || U16(data + pos + 2) != 1) return false;
    pos += 4;
    struct Record { std::string owner, target, ip; unsigned ttl; };
    std::vector<Record> records;
    for (unsigned i = 0; i < U16(data + 6); ++i) {
        if (!Name(data, size, pos, name) || size - pos < 10) return false;
        unsigned type = U16(data + pos), cls = U16(data + pos + 2), ttl = U32(data + pos + 4), n = U16(data + pos + 8);
        pos += 10; if (n > size - pos) return false;
        Record r; r.owner = name; r.ttl = ttl;
        if (cls == 1 && type == 1 && n == 4) {
            for (unsigned j = 0; j < 4; ++j) { if (j) r.ip += '.'; r.ip += std::to_string(data[pos + j]); }
            records.push_back(r);
        } else if (cls == 1 && type == 5) {
            size_t end = pos;
            if (!Name(data, size, end, r.target) || end != pos + n) return false;
            records.push_back(r);
        }
        pos += n;
    }
    result = Answer(); name = Lower(host);
    for (unsigned depth = 0; depth < 16; ++depth) {
        std::string next;
        for (const auto& r : records) if (r.owner == name) {
            if (!r.ip.empty()) {
                if (!result.addresses.empty()) result.addresses += ',';
                result.addresses += r.ip;
                if (r.ttl < result.ttl) result.ttl = r.ttl;
            }
            if (!r.target.empty()) { next = r.target; if (r.ttl < result.ttl) result.ttl = r.ttl; }
        }
        if (!result.addresses.empty()) return true;
        if (next.empty() || next == name) return false;
        name = next;
    }
    return false;
}
}
