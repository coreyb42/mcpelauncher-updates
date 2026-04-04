#include <playapi/util/http.h>

#include <cassert>
#include <zlib.h>
#include <cstring>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <iomanip>
#include <cstring>

using namespace playapi;

namespace Utility {
    std::string CharToHex(unsigned char c) {
        short i = c;
         
        std::ostringstream s;
         
        s << "%" << std::setw(2) << std::setfill('0') << std::hex << i;
         
        return s.str();
    }

    std::string UrlEncode(const std::string &toEncode) {
        std::ostringstream out;
         
        for(std::string::size_type i=0; i < toEncode.length(); ++i) {
            short t = toEncode.at(i);
             
            if(
                (t >= 45 && t <= 46) ||       // hyphen-period
                (t >= 48 && t <= 57) ||       // 0-9
                (t >= 65 && t <= 90) ||       // A-Z
                t == 95 ||          // underscore
                (t >= 97 && t <= 122) ||  // a-z
                t == 126            // tilde
            ) {
                out << toEncode.at(i);
            } else {
                out << CharToHex(toEncode.at(i));
            }
        }
         
        return out.str();
    }
}

void url_encoded_entity::add_pair(const std::string& key, const std::string& val) {
    pairs.push_back({key, val});
}


std::string url_encoded_entity::encode() const {
    std::stringstream out;
    int i = 0;
    for (auto&& p : pairs) {
        if(i++) {
            out << "&";
        }
        out << Utility::UrlEncode(p.first) << "=" << Utility::UrlEncode(p.second);
    }
    return out.str();
}

http_response::http_response(bool ok, long statusCode, std::string body) :
        ok(ok), statusCode(statusCode), body(std::move(body)) {
    //
}

http_response::http_response(http_response&& r) : ok(r.ok), statusCode(r.statusCode),
                                                  body(r.body) {
    r.ok = false;
    r.statusCode = 0;
    r.body = std::string();
}

http_response& http_response::operator=(http_response&& r) {
    ok = r.ok;
    body = r.body;
    r.ok = false;
    r.body = std::string();
    return *this;
}

http_response::~http_response() {
}

void http_request::set_body(const url_encoded_entity& ent) {
    set_body(ent.encode());
}

void http_request::add_header(const std::string& key, const std::string& value) {
    headers[key] = value;
}

void http_request::set_gzip_body(const std::string& str) {
    z_stream zs;
    zs.zalloc = Z_NULL;
    zs.zfree = Z_NULL;
    zs.opaque = Z_NULL;
    int ret = deflateInit2(&zs, Z_BEST_COMPRESSION, Z_DEFLATED, 31, 8, Z_DEFAULT_STRATEGY);
    assert(ret == Z_OK);

    zs.avail_in = (uInt) str.length();
    zs.next_in = (unsigned char*) str.data();
    std::string out;
    while(true) {
        out.resize(out.size() + 4096);
        zs.avail_out = 4096;
        zs.next_out = (unsigned char*) out.data();
        ret = deflate(&zs, Z_FINISH);
        assert(ret != Z_STREAM_ERROR);
        if (zs.avail_out != 0) {
            out.resize(out.size() - zs.avail_out);
            break;
        }
    }
    deflateEnd(&zs);
    body = std::move(out);
}

// http_response http_request::perform() {
//     std::stringstream output;
//     // abort();
//     return http_response(false, 0, output.str());
// }

void http_request::perform(std::function<void(http_response)> success, std::function<void(std::exception_ptr)> error) {
    success(perform());
}