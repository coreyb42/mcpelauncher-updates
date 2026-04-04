#pragma once

#include <mutex>
#include <future>
#include <playapi/api.h>
#include <playapi/device_info.h>
#include <playapi/login.h>
#include <playapi/file_login_cache.h>
#include <zlib.h>
#include <jni.h>
#include <fstream>
#include <sys/stat.h>

#include "types.hpp"

#ifdef __aarch64__
#define ARCH_FOLDER "arm64-v8a"
#else
#define ARCH_FOLDER "x86_64"
#endif

using dlsym_fn = void *(*)(void *handle, const char *name);

struct HttpClientResult {
  jobject response;
  bool success;
  std::string error_message;
};

struct SyncHttpRequest {
  std::promise<HttpClientResult> response_promise;
};

static void OnRequestCompleted(JNIEnv* env, jobject thiz, jlong source_call, jobject response) {
    // dummy function to force JNIVM to not strip this symbol
    IF_DEBUG(std::cout << "OnRequestCompleted called: " << source_call << " " << response << std::endl);
    SyncHttpRequest* req = reinterpret_cast<SyncHttpRequest*>(source_call);
    req->response_promise.set_value(HttpClientResult{env->NewGlobalRef(response), true, ""});
}

static void OnRequestFailed(JNIEnv* env, jobject thiz, jlong source_call, jstring error) {
    // dummy function to force JNIVM to not strip this symbol
    const char* error_cstr = env->GetStringUTFChars(error, nullptr);
    IF_DEBUG(std::cout << "OnRequestFailed called: " << source_call << " " << error_cstr << std::endl);
    SyncHttpRequest* req = reinterpret_cast<SyncHttpRequest*>(source_call);
    req->response_promise.set_value(HttpClientResult{nullptr, false, std::string(error_cstr)});
    env->ReleaseStringUTFChars(error, error_cstr);}

static void OnRequestFailed2(JNIEnv* env, jobject thiz, jlong source_call, jstring error, jstring error2, jstring error3, jboolean flag) {
    // dummy function to force JNIVM to not strip this symbol
    const char* error_cstr = env->GetStringUTFChars(error, nullptr);
    const char* error2_cstr = env->GetStringUTFChars(error2, nullptr);
    const char* error3_cstr = env->GetStringUTFChars(error3, nullptr);
    IF_DEBUG(std::cout << "OnRequestFailed2 called: " << source_call << " " << error_cstr << " " << error2_cstr << " " << error3_cstr << " " << flag << std::endl);
    SyncHttpRequest* req = reinterpret_cast<SyncHttpRequest*>(source_call);
    req->response_promise.set_value(HttpClientResult{nullptr, false, std::string(error_cstr) + " " + std::string(error2_cstr) + " " + std::string(error3_cstr)});
    env->ReleaseStringUTFChars(error, error_cstr);
    env->ReleaseStringUTFChars(error2, error2_cstr);
    env->ReleaseStringUTFChars(error3, error3_cstr);
}

static JNIEnv* env = nullptr;

// Return true on success, false on failure
bool zlib_inflate(const std::vector<uint8_t>& compressed, std::string& out) {
    z_stream zs{};
    zs.next_in = const_cast<Bytef*>(compressed.data());
    zs.avail_in = compressed.size();

    if (inflateInit2(&zs, 15 + 32) != Z_OK) {
        IF_DEBUG(std::cout << "inflateInit2 failed" << std::endl);
        return false;
    }

    char buffer[32768]; // 32 KB chunks
    int ret;
    out.clear();

    do {
        zs.next_out = reinterpret_cast<Bytef*>(buffer);
        zs.avail_out = sizeof(buffer);

        ret = inflate(&zs, Z_NO_FLUSH);
        if (ret != Z_OK && ret != Z_STREAM_END) {
            inflateEnd(&zs);
            IF_DEBUG(std::cout << "inflate failed with error code: " << ret << std::endl);
            return false;
        }

        // Append decompressed chunk
        out.append(buffer, sizeof(buffer) - zs.avail_out);
    } while (ret != Z_STREAM_END);

    inflateEnd(&zs);
    return true;
}

playapi::http_response playapi::http_request::perform() {
    jclass HttpClientRequest = env->FindClass(cryptstring("com/xbox/httpclient/HttpClientRequest"));
    jmethodID createClientRequest = env->GetStaticMethodID(HttpClientRequest, cryptstring("createClientRequest"), cryptstring("())Lcom/xbox/httpclient/HttpClientRequest;"));
    jobject req = env->CallStaticObjectMethodA(HttpClientRequest, createClientRequest, nullptr);
    jmethodID setHttpMethodAndBody = env->GetMethodID(HttpClientRequest, cryptstring("setHttpMethodAndBody"), cryptstring("(Ljava/lang/String;Ljava/lang/String;[B)V"));
    jmethodID setHttpUrl = env->GetMethodID(HttpClientRequest, cryptstring("setHttpUrl"), cryptstring("(Ljava/lang/String;)V"));
    jmethodID doRequestAsync = env->GetMethodID(HttpClientRequest, cryptstring("doRequestAsync"), cryptstring("(J)V"));
    jmethodID setHttpHeader = env->GetMethodID(HttpClientRequest, cryptstring("setHttpHeader"), cryptstring("(Ljava/lang/String;Ljava/lang/String;)V"));
    env->CallVoidMethodA(req, setHttpUrl, std::initializer_list<jvalue>{jvalue{.l = env->NewStringUTF(this->url.c_str())}}.begin());

    std::string method;
    switch (this->method)
    {
    case playapi::http_method::GET:
        method = (std::string)cryptstring("GET");
        break;
    case playapi::http_method::POST:
        method = (std::string)cryptstring("POST");
        break;
    case playapi::http_method::PUT:
        method = (std::string)cryptstring("PUT");
        break;
    default:
        method = (std::string)cryptstring("GET");
        break;
    }
    jbyteArray bodyBytes = nullptr;
    if(body.size() > 0) {
        bodyBytes = env->NewByteArray((jsize)body.size());
        env->SetByteArrayRegion(bodyBytes, 0, (jsize)body.size(), reinterpret_cast<const jbyte*>(body.data()));
    }
    env->CallVoidMethodA(req, setHttpMethodAndBody, std::initializer_list<jvalue>{ jvalue{.l = env->NewStringUTF(method.c_str())}, jvalue{.l = env->NewStringUTF("")}, jvalue{.l = bodyBytes}}.begin());
    if(!this->user_agent.empty())
      env->CallVoidMethodA(req, setHttpHeader, std::initializer_list<jvalue>{ jvalue{.l = env->NewStringUTF(cryptstring("User-Agent").c_str())}, jvalue{.l = env->NewStringUTF(this->user_agent.c_str())} }.begin());
    for (auto&& h : headers)
    {
        env->CallVoidMethodA(req, setHttpHeader, std::initializer_list<jvalue>{ jvalue{.l = env->NewStringUTF(h.first.c_str())}, jvalue{.l = env->NewStringUTF(h.second.c_str())} }.begin());
    }
    SyncHttpRequest request;
    env->CallVoidMethodA(req, doRequestAsync, std::initializer_list<jvalue>{jvalue{.j = (jlong)&request}}.begin());
    HttpClientResult result = request.response_promise.get_future().get();
    jclass HttpClientResponse = env->FindClass(cryptstring("com/xbox/httpclient/HttpClientResponse"));
    jmethodID getResponseCode = env->GetMethodID(HttpClientResponse, cryptstring("getResponseCode"), cryptstring("()I"));
    jmethodID getResponseBodyBytes = env->GetMethodID(HttpClientResponse, cryptstring("getResponseBodyBytes"), cryptstring("()[B"));
    jmethodID getNumHeaders = env->GetMethodID(HttpClientResponse, cryptstring("getNumHeaders"), cryptstring("()I"));
    jmethodID getHeaderNameAtIndex = env->GetMethodID(HttpClientResponse, cryptstring("getHeaderNameAtIndex"), cryptstring("(I)Ljava/lang/String;"));
    jmethodID getHeaderValueAtIndex = env->GetMethodID(HttpClientResponse, cryptstring("getHeaderValueAtIndex"), cryptstring("(I)Ljava/lang/String;"));
    if(result.success) {
      jint response_code = env->CallIntMethodA(result.response, getResponseCode, nullptr);
      IF_DEBUG(std::cout << "Response Code: " << response_code << std::endl);
      jint numHeaders = env->CallIntMethodA(result.response, getNumHeaders, nullptr);
      IF_DEBUG(std::cout << "Response Headers:" << std::endl);
      std::string redirectUrl;
      for(jint i = 0; i < numHeaders; i++) {
        jstring name = (jstring)env->CallObjectMethodA(result.response, getHeaderNameAtIndex, std::initializer_list<jvalue>{jvalue{.i = i}}.begin());
        jstring value = (jstring)env->CallObjectMethodA(result.response, getHeaderValueAtIndex, std::initializer_list<jvalue>{jvalue{.i = i}}.begin());
        const char* name_cstr = env->GetStringUTFChars(name, nullptr);
        const char* value_cstr = env->GetStringUTFChars(value, nullptr);
        IF_DEBUG(std::cout << name_cstr << ": " << value_cstr << std::endl);

        if(this->follow_location && response_code == 302) {
          std::string name = name_cstr;
          std::transform(name.begin(), name.end(), name.begin(), ::tolower);
          if(name == "location") {
            redirectUrl = value_cstr;
          }
        }

        env->ReleaseStringUTFChars(name, name_cstr);
        env->ReleaseStringUTFChars(value, value_cstr);
      }
      if(response_code == 302) {
        if(redirectUrl.empty()) {
          IF_DEBUG(std::cout << "Missing redirect URL" << std::endl);
          abort();
        }
        this->url = redirectUrl;
        IF_DEBUG(std::cout << "Redirect to " << this->url << std::endl);
        return this->perform();
      }
      jbyteArray bodyBytes = (jbyteArray)env->CallObjectMethodA(result.response, getResponseBodyBytes, nullptr);
      jsize bodyLength = env->GetArrayLength(bodyBytes);
      std::vector<unsigned char> body(bodyLength);
      env->GetByteArrayRegion(bodyBytes, 0, bodyLength, reinterpret_cast<jbyte*>(body.data()));

      std::string result;
      if(this->encoding.find(cryptstring("gzip").c_str()) != std::string::npos) {
          if(!zlib_inflate(body, result)) {
              IF_DEBUG(std::cout << "Failed to decompress gzip response" << std::endl);
              return http_response(false, response_code, cryptstring("Failed to decompress gzip response"));
          }
      } else {
          result = std::string(body.begin(), body.end());
      }
      // IF_DEBUG(std::cout << "Response Body (" << bodyLength << " bytes): " << result << std::endl);
      return http_response(true, response_code, result);
    } else {
      IF_DEBUG(std::cout << "Request failed: " << result.error_message << std::endl);
      return http_response(false, 0, result.error_message);
    }
}

template<typename T> static void registerNatives(JNIEnv* env, jclass cl, const T& natives) {
    if(env == nullptr || cl == nullptr) {
        IF_DEBUG(std::cout << "natives:" << natives.size() << std::endl);
        return;
    }
    env->RegisterNatives(cl, natives.begin(), natives.size());
}

template<typename T> static void registerNativesFromGame(JNIEnv* env, jclass cl, const T& natives) {
  static auto dlsym_ptr = (dlsym_fn)dlsym(RTLD_NEXT, cryptstring("dlsym"));

  auto dlopen_ptr =
    (void *(*)(const char *, int))dlsym_ptr(RTLD_NEXT, cryptstring("dlopen"));

  std::vector<JNINativeMethod> javaEntries;
  auto lib = dlopen_ptr(cryptstring("libminecraftpe.so"), RTLD_NOLOAD);
if(lib == nullptr) {
    return;
  }
  std::string cppClassName = cryptstring("com_xbox_httpclient_HttpClientRequest");
  for(auto const &ent : natives) {
      auto cppSymName = std::string(cryptstring("Java_")) + cppClassName + (const char*)cryptstring("_") + ent.name;
      auto cppSym = dlsym(lib, cppSymName.c_str());
      if(cppSym == nullptr) {
          continue;
      }
      javaEntries.push_back({(char *)ent.name, (char *)ent.signature, cppSym});
  }
  if(env == nullptr || cl == nullptr) {
    IF_DEBUG(std::cout << "natives:" << javaEntries.size() << std::endl);
    return;
  }
  env->RegisterNatives(cl, javaEntries.data(), javaEntries.size());
}

// TODO machine id
#define OUR_ENCRYPTED_TOKEN cryptstring("PLACE_HOLDERDFDFEFEFS") // Placeholder for the encrypted token

ssize_t find_eocd_backward(const uint8_t* data, size_t len){
    if(len < 4) return -1;
    size_t start = (len > 65536 + 22) ? len - (65536 + 22) : 0;
    for(size_t i = len - 4 + 1; i-- > start; ){
        if(data[i]==0x50 && data[i+1]==0x4B && data[i+2]==0x05 && data[i+3]==0x06) return (ssize_t)i;
    }
    return -1;
}

uint32_t le32(const uint8_t* p){ return uint32_t(p[0]) | (uint32_t(p[1])<<8) | (uint32_t(p[2])<<16) | (uint32_t(p[3])<<24); }

static void writeByte(std::ostream& hashstream, size_t b) {
  unsigned char c = static_cast<unsigned char>(b);
  hashstream.write((char*)&c, 1);
}

static void writeHash(std::ostream& hashstream, size_t hashed) {
  writeByte(hashstream, hashed & 0xFF);
  writeByte(hashstream, (hashed >> 8) & 0xFF);
  writeByte(hashstream, (hashed >> 16) & 0xFF);
  writeByte(hashstream, (hashed >> 24) & 0xFF);
  writeByte(hashstream, (hashed >> 32) & 0xFF);
  writeByte(hashstream, (hashed >> 40) & 0xFF);
  writeByte(hashstream, (hashed >> 48) & 0xFF);
  writeByte(hashstream, (hashed >> 56) & 0xFF);
}

static bool doNotAskAgain = false;

void validate(void(*cbk)()) {
  if(doNotAskAgain) {
    return;
  }
  static auto dlsym_ptr = (dlsym_fn)dlsym(RTLD_NEXT, cryptstring("dlsym"));
  static auto callback = cbk;

  auto dlopen_ptr =
    (void *(*)(const char *, int))dlsym_ptr(RTLD_NEXT, cryptstring("dlopen"));

    static auto mcpelauncher_mod = dlopen_ptr(cryptstring("libmcpelauncher_mod.so"), RTLD_NOLOAD);
  if(mcpelauncher_mod == nullptr) {
    IF_DEBUG(std::cout << cryptstring("mcpelauncher_mod not found") << std::endl);
    return;
  }

  struct GoogleCredentials {
    const char* email;
    const char* token;
  };

    static jint (*orginal_JNI_OnLoad)(JavaVM* vm, void* reserved);

  auto lambda = +[](JavaVM* vm, void* reserved) -> jint {
    IF_DEBUG(std::cout << cryptstring("JNI_OnLoad") << std::endl);
    std::ifstream token_file(cryptstring("/data/data/com.mojang.minecraftpe/pass.token").c_str(), std::ios::binary);
    if(token_file.is_open()) {
        auto token = OUR_ENCRYPTED_TOKEN;
        std::stringstream val;
        val << token_file.rdbuf();
        token_file.close();
        if(val.str() == std::string(token)) {
            callback();
            if(vm != nullptr) {
              return orginal_JNI_OnLoad(vm, reserved);
            } else {
              return 0;
            }
        }
    }
    if(vm != nullptr) {
      JNIEnv* env = nullptr;
      vm->GetEnv((void**)&env, JNI_VERSION_1_6);
      ::env = env;
    }
    IF_DEBUG(std::cout << cryptstring("register natives") << std::endl);
    jclass HttpClientRequest = env->FindClass(cryptstring("com/xbox/httpclient/HttpClientRequest"));
    env->UnregisterNatives(HttpClientRequest);
    registerNatives(env, HttpClientRequest, std::initializer_list<JNINativeMethod> {
      { cryptstring("OnRequestCompleted"), cryptstring("(JLcom/xbox/httpclient/HttpClientResponse;)V"), (void*)OnRequestCompleted },
      { cryptstring("OnRequestFailed"), cryptstring("(JLjava/lang/String;Ljava/lang/String;Ljava/lang/String;Z)V"), (void*)OnRequestFailed2 },
      { cryptstring("OnRequestCompleted"), cryptstring("(JLjava/lang/String;)V"), (void*)OnRequestFailed }
    });

    playapi::device_info device;
    device.config_native_platforms = {
ARCH_FOLDER
    };
    playapi::api api(device);
    playapi::file_login_cache login_cache(cryptstring("/data/data/com.mojang.minecraftpe/playapi_cache.dat"));
    login_cache.clear();
    playapi::login_api login_api(device, login_cache);
    static playapi::login_api* login = nullptr;
    login = &login_api;

    auto mcpelauncher_request_google_credentials =
    (void (*)(void (*onsuccess)(GoogleCredentials creds), void (*onfailure)(const char* error)))dlsym_ptr(mcpelauncher_mod, cryptstring("mcpelauncher_request_google_credentials"));
    if(!mcpelauncher_request_google_credentials) {
      IF_DEBUG(std::cout << cryptstring("mcpelauncher_request_google_credentials not found") << std::endl);
      return 0;
    }

    mcpelauncher_request_google_credentials(+[](GoogleCredentials creds) {
      IF_DEBUG(std::cout << "Google credentials obtained: " << creds.email << " " << creds.token << std::endl);
      login->set_token(creds.email, creds.token);
    }, +[](const char* error) {
      IF_DEBUG(std::cout << "Failed to obtain Google credentials: " << error << std::endl);
      login = nullptr;
      doNotAskAgain = true;
    });
    if(login == nullptr) {
      if(vm != nullptr) {
        IF_DEBUG(std::cout << cryptstring("call original") << std::endl);
        jint ret = orginal_JNI_OnLoad(vm, reserved);
      }
      return 0;
    }
    login = nullptr;

    playapi::checkin_api checkin(device);
    checkin.add_auth(login_api)->call();
    device.generate_fields();
    auto checkin_data = checkin.perform_checkin()->call();

    api.set_auth(login_api)->call();
    api.set_checkin_data(checkin_data);

    if (api.toc_cookie.length() == 0 || api.device_config_token.length() == 0) {
        api.fetch_user_settings()->call();
        auto toc = api.fetch_toc()->call();
        if (toc.payload().tocresponse().has_cookie())
            api.toc_cookie = toc.payload().tocresponse().cookie();

        if (api.fetch_toc()->call().payload().tocresponse().requiresuploaddeviceconfig()) {
            auto resp = api.upload_device_config()->call();
            api.device_config_token = resp.payload().uploaddeviceconfigresponse().uploaddeviceconfigtoken();

            toc = api.fetch_toc()->call();
            assert(!toc.payload().tocresponse().requiresuploaddeviceconfig() &&
                   toc.payload().tocresponse().has_cookie());
            api.toc_cookie = toc.payload().tocresponse().cookie();
            if (toc.payload().tocresponse().has_toscontent() && toc.payload().tocresponse().has_tostoken()) {
                bool allow_marketing_emails = false;
                auto tos = api.accept_tos(toc.payload().tocresponse().tostoken(), allow_marketing_emails)->call();
                assert(tos.payload().has_accepttosresponse());
            }
        }
    }
    api.details("com.mojang.minecraftpe")->call([&api](playapi::proto::finsky::response::ResponseWrapper&& resp) {
      auto details = resp.payload().detailsresponse().docv2();
      if(details.details().appdetails().versionstring() == "" || !details.details().appdetails().versioncode()) {
          return;
      }
      auto code = 
#ifdef __aarch64__
        972600002
#else
        982600002
#endif
      ;
      api.delivery("com.mojang.minecraftpe", code, std::string())->call([](playapi::proto::finsky::response::ResponseWrapper&& resp) {
          auto dd = resp.payload().deliveryresponse().appdeliverydata();
          auto url = (dd.has_gzippeddownloadurl() ? dd.gzippeddownloadurl() : dd.downloadurl());
          IF_DEBUG(std::cout << "Apk Url: " << url << std::endl);
          if(url.size() == 0) {
            return;
          }
          for(int i = 0; i < dd.splitdeliverydata().size(); i++) {
            auto data = dd.splitdeliverydata(i);
            IF_DEBUG(std::cout << "Id: " << data.id() << std::endl);
            if(data.has_downloadurl() && (data.id() == "config.x86" || data.id() == "config.x86_64" || data.id() == "config.arm64_v8a")) {
              IF_DEBUG(std::cout << "Native Url: " << data.downloadurl() << std::endl);
              auto http_range_get = [&](const std::string &url, uint64_t start, uint64_t end) {
                auto cookie = dd.downloadauthcookie(0);
                playapi::http_request req(data.downloadurl());
                req.set_method(playapi::http_method::GET);
                req.add_header("Accept-Encoding", "identity");
                req.add_header("Cookie", cookie.name() + "=" + cookie.value());
                req.add_header("Range", (std::stringstream() << "bytes=" << start << "-" << end).str());
                playapi::device_info device;
                req.set_user_agent("AndroidDownloadManager/" + device.build_version_string + " (Linux; U; Android " +
                      device.build_version_string + "; " + device.build_model + " Build/" + device.build_id + ")");

                req.set_follow_location(true);
                req.set_timeout(0L);
                std::cout << std::endl << "Starting download...";
                auto resp = req.perform();
                std::cout << std::endl << "Finished download?";
                return resp.get_body();
              };

              // For brevity assume we know file_size; fetch last 66KiB
              uint64_t file_size = data.downloadsize();
              uint64_t tail_start = (file_size > 67000) ? file_size - 67000 : 0;
              auto tail = http_range_get(url, tail_start, file_size - 1);
              ssize_t eocd_off = find_eocd_backward((const uint8_t*)tail.data(), tail.size());
              if(eocd_off < 0){ std::cerr<<"EOCD not found\n"; abort(); }
              size_t eocd_pos = tail_start + eocd_off;
              const uint8_t* e = (const uint8_t*)tail.data() + eocd_off;
              uint32_t cd_size = le32(e + 12);
              uint32_t cd_offset = le32(e + 16);
              // 2) fetch central directory
              auto cd = http_range_get(url, cd_offset, cd_offset + cd_size - 1);
              // 3) parse first central dir entry (loop in real code)
              // central dir header signature 0x02014b50
              size_t p = 0;
              std::vector<uint8_t> out(0x25000000);
Dl_info info;
              dladdr((void*)&find_eocd_backward, &info);
              std::filesystem::path outPath{std::filesystem::path(info.dli_fname).parent_path() / cryptstring("patches") / "v1.26.0.2/" ARCH_FOLDER};
              std::filesystem::create_directories(outPath);
              while(p + 46 <= cd.size()){
                  if(le32((const uint8_t*)cd.data()+p) != 0x02014b50) break;
                  uint16_t name_len = uint16_t(cd[p+28]) | (uint16_t(cd[p+29])<<8);
                  uint16_t extra_len = uint16_t(cd[p+30]) | (uint16_t(cd[p+31])<<8);
                  uint16_t comment_len = uint16_t(cd[p+32]) | (uint16_t(cd[p+33])<<8);
                  uint32_t comp_size = le32((const uint8_t*)cd.data()+p+20);
                  uint32_t local_off = le32((const uint8_t*)cd.data()+p+42);
                  std::string name((char*)cd.data()+p+46, name_len);
                  std::cout<<"Entry: "<<name<<" comp_size="<<comp_size<<" local_off="<<local_off<<"\n";
                  p += 46 + name_len + extra_len + comment_len;
                  if(name.find("libmaesdk.so") == std::string::npos || name.rfind("lib", 0) != 0 || name.find("libpairipcore.so") != std::string::npos || name.find("libminecraftpe.so") != std::string::npos || name.find("libPlayFabMultiplayer.so") != std::string::npos || comp_size > 0x250000) {
                    continue;
                  }
                  // 4) fetch compressed data by reading local header then data
                  auto local_hdr = http_range_get(url, local_off, local_off + 30 + name_len - 1);
                  uint16_t lh_name_len = uint16_t(local_hdr[26]) | (uint16_t(local_hdr[27])<<8);
                  uint64_t data_start = local_off + 30 + lh_name_len;
                  auto comp = http_range_get(url, data_start, data_start + comp_size - 1);
                  // 5) inflate raw deflate
                  z_stream zs{}; inflateInit2(&zs, -MAX_WBITS);
                  zs.next_in = (uint8_t*)comp.data(); zs.avail_in = comp.size();
                  zs.next_out = out.data(); zs.avail_out = out.size();
                  int r = inflate(&zs, Z_FINISH);
                  if(r==Z_STREAM_END) std::cout<<"Decompressed "<<(out.size()-zs.avail_out)<<" bytes\n";
                  inflateEnd(&zs);
                  FILE* file = fopen((outPath / std::filesystem::path(name).filename()).string().data(), "w");
                  fwrite(out.data(), sizeof(char), out.size()-zs.avail_out, file);
                  fclose(file);
              }
            }
          }

          std::ofstream token_file(cryptstring("/data/data/com.mojang.minecraftpe/mcpelauncher-updates-oss.pass").c_str(), std::ios::binary);
          token_file << OUR_ENCRYPTED_TOKEN;
          token_file.close();
          callback();
      }, [](std::exception_ptr ptr) {});
    }, [](std::exception_ptr ptr) {});
    
    env->UnregisterNatives(HttpClientRequest);
    registerNativesFromGame(env, HttpClientRequest, std::initializer_list<JNINativeMethod> {
      {cryptstring("OnRequestCompleted"), cryptstring("(JLcom/xbox/httpclient/HttpClientResponse;)V")},
      {cryptstring("OnRequestFailed"), cryptstring("(JLjava/lang/String;)V")},
      {cryptstring("OnRequestFailed"), cryptstring("(JLjava/lang/String;Ljava/lang/String;Ljava/lang/String;Z)V")}
    });
    if(vm != nullptr) {
      IF_DEBUG(std::cout << cryptstring("call original") << std::endl);
      jint ret = orginal_JNI_OnLoad(vm, reserved);
    }
    return 0;
  };

  if(::env != nullptr) {
    lambda(nullptr, nullptr);
    return;
  }
auto mcpelauncher_relocate =
    (void (*)(void* handle, const char* name, void* hook))dlsym_ptr(mcpelauncher_mod, cryptstring("mcpelauncher_relocate"));
  
  if(!mcpelauncher_relocate) {
    IF_DEBUG(std::cout << cryptstring("mcpelauncher_relocate not found") << std::endl);
    return;
  }

  auto mc = dlopen_ptr(cryptstring("libminecraftpe.so"), RTLD_NOLOAD);
  if(mc == nullptr) {
    IF_DEBUG(std::cout << cryptstring("libminecraftpe.so not found") << std::endl);
    return;
  }

  auto fmod = dlopen_ptr(cryptstring("libfmod.so"), RTLD_NOLOAD);
  orginal_JNI_OnLoad =
    (jint (*)(JavaVM* vm, void* reserved))dlsym_ptr(fmod, cryptstring("JNI_OnLoad"));
  auto found = fmod;
  if(!orginal_JNI_OnLoad) {
    orginal_JNI_OnLoad = (jint (*)(JavaVM* vm, void* reserved))dlsym_ptr(mc, cryptstring("JNI_OnLoad"));
    found = mc;
    if(!orginal_JNI_OnLoad) {
      IF_DEBUG(std::cout << cryptstring("original JNI_OnLoad not found") << std::endl);
      return;
    }
  }

  mcpelauncher_relocate(found, cryptstring("JNI_OnLoad"), (void*) lambda);
}