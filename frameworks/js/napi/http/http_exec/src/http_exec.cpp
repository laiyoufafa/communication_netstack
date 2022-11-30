/*
 * Copyright (c) 2021-2022 Huawei Device Co., Ltd.
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <memory>
#include <thread>

#include "cache_proxy.h"
#include "constant.h"
#include "event_list.h"
#include "http_async_work.h"
#include "http_time.h"
#include "napi_utils.h"
#include "netstack_common_utils.h"
#include "netstack_log.h"
#include "securec.h"

#include "http_exec.h"

#define NETSTACK_CURL_EASY_SET_OPTION(handle, opt, data, asyncContext)                                   \
    do {                                                                                                 \
        CURLcode result = curl_easy_setopt(handle, opt, data);                                           \
        if (result != CURLE_OK) {                                                                        \
            const char *err = curl_easy_strerror(result);                                                \
            NETSTACK_LOGE("Failed to set option: %{public}s, %{public}s %{public}d", #opt, err, result); \
            (asyncContext)->SetErrorCode(result);                                                        \
            return false;                                                                                \
        }                                                                                                \
    } while (0)

namespace OHOS::NetStack {
static constexpr size_t MAX_LIMIT = 5 * 1024 * 1024;
static constexpr int CURL_TIMEOUT_MS = 100;
bool HttpExec::AddCurlHandle(CURL *handle, RequestContext *context)
{
    if (handle == nullptr || curlMulti_ == nullptr) {
        NETSTACK_LOGE("handle nullptr");
        return false;
    }

    std::lock_guard guard(curlMultiMutex_);
    if (curl_multi_add_handle(curlMulti_, handle) != CURLM_OK) {
        return false;
    }

    contextMap_[handle] = context;
    return true;
}

std::mutex HttpExec::mutex_;
std::mutex HttpExec::curlMultiMutex_;
CURLM *HttpExec::curlMulti_ = nullptr;
std::map<CURL *, RequestContext *> HttpExec::contextMap_;
#ifndef MAC_PLATFORM
std::atomic_bool HttpExec::initialized_(false);
#else
bool HttpExec::initialized_ = false;
#endif // MAC_PLATFORM

bool HttpExec::RequestWithoutCache(RequestContext *context)
{
    if (!initialized_) {
        NETSTACK_LOGE("curl not init");
        return false;
    }

    auto handle = curl_easy_init();
    if (!handle) {
        NETSTACK_LOGE("Failed to create fetch task");
        return false;
    }

    std::vector<std::string> vec;
    std::for_each(context->options.GetHeader().begin(), context->options.GetHeader().end(),
                  [&vec](const std::pair<std::string, std::string> &p) {
                      vec.emplace_back(p.first + HttpConstant::HTTP_HEADER_SEPARATOR + p.second);
                  });
    context->SetCurlHeaderList(MakeHeaders(vec));

    if (!SetOption(handle, context, context->GetCurlHeaderList())) {
        NETSTACK_LOGE("set option failed");
        return false;
    }

    context->response.SetRequestTime(HttpTime::GetNowTimeGMT());

    if (!AddCurlHandle(handle, context)) {
        NETSTACK_LOGE("add handle failed");
        return false;
    }

    return true;
}

bool HttpExec::GetCurlDataFromHandle(CURL *handle, RequestContext *context, CURLMSG curlMsg, CURLcode result)
{
    if (curlMsg != CURLMSG_DONE) {
        NETSTACK_LOGE("CURLMSG %{public}s", std::to_string(curlMsg).c_str());
        context->SetErrorCode(NapiUtils::NETSTACK_NAPI_INTERNAL_ERROR);
        return false;
    }

    if (result != CURLE_OK) {
        context->SetErrorCode(result);
        NETSTACK_LOGE("CURLcode result %{public}s", std::to_string(result).c_str());
        return false;
    }

    context->response.SetResponseTime(HttpTime::GetNowTimeGMT());

    int64_t responseCode;
    CURLcode code = curl_easy_getinfo(handle, CURLINFO_RESPONSE_CODE, &responseCode);
    if (code != CURLE_OK) {
        context->SetErrorCode(code);
        return false;
    }
    context->response.SetResponseCode(responseCode);
    if (context->response.GetResponseCode() == static_cast<uint32_t>(ResponseCode::NOT_MODIFIED)) {
        NETSTACK_LOGI("cache is NOT_MODIFIED, we use the cache");
        context->SetResponseByCache();
        return true;
    }
    NETSTACK_LOGD("responseCode is %{public}s", std::to_string(responseCode).c_str());

    struct curl_slist *cookies = nullptr;
    code = curl_easy_getinfo(handle, CURLINFO_COOKIELIST, &cookies);
    if (code != CURLE_OK) {
        context->SetErrorCode(code);
        return false;
    }

    std::unique_ptr<struct curl_slist, decltype(&curl_slist_free_all)> cookiesHandle(cookies, curl_slist_free_all);
    while (cookies) {
        context->response.AppendCookies(cookies->data, strlen(cookies->data));
        if (cookies->next != nullptr) {
            context->response.AppendCookies(HttpConstant::HTTP_LINE_SEPARATOR,
                                            strlen(HttpConstant::HTTP_LINE_SEPARATOR));
        }
        cookies = cookies->next;
    }
    context->response.ParseHeaders();

    return true;
}

void HttpExec::HandleCurlData(CURLMsg *msg)
{
    if (msg == nullptr) {
        return;
    }

    auto handle = msg->easy_handle;
    if (handle == nullptr) {
        return;
    }

    auto it = contextMap_.find(handle);
    if (it == contextMap_.end()) {
        NETSTACK_LOGE("can not find context");
        return;
    }

    auto context = it->second;
    contextMap_.erase(it);
    if (context == nullptr) {
        NETSTACK_LOGE("can not find context");
        return;
    }
    context->SetExecOK(GetCurlDataFromHandle(handle, context, msg->msg, msg->data.result));
    if (context->IsExecOK()) {
        CacheProxy proxy(context->options);
        proxy.WriteResponseToCache(context->response);
    }

    if (context->GetManager()->IsManagerValid()) {
        NapiUtils::CreateUvQueueWorkEnhanced(context->GetEnv(), context, HttpAsyncWork::RequestCallback);
    }
}

bool HttpExec::ExecRequest(RequestContext *context)
{
    context->options.SetRequestTime(HttpTime::GetNowTimeGMT());
    CacheProxy proxy(context->options);
    if (context->IsUsingCache() && proxy.ReadResponseFromCache(context)) {
        return true;
    }

    if (!RequestWithoutCache(context)) {
        context->SetErrorCode(NapiUtils::NETSTACK_NAPI_INTERNAL_ERROR);
        if (context->GetManager()->IsManagerValid()) {
            NapiUtils::CreateUvQueueWorkEnhanced(context->GetEnv(), context, HttpAsyncWork::RequestCallback);
        }
        return false;
    }

    return true;
}

napi_value HttpExec::RequestCallback(RequestContext *context)
{
    napi_value object = NapiUtils::CreateObject(context->GetEnv());
    if (NapiUtils::GetValueType(context->GetEnv(), object) != napi_object) {
        return nullptr;
    }

    NapiUtils::SetUint32Property(context->GetEnv(), object, HttpConstant::RESPONSE_KEY_RESPONSE_CODE,
                                 context->response.GetResponseCode());
    NapiUtils::SetStringPropertyUtf8(context->GetEnv(), object, HttpConstant::RESPONSE_KEY_COOKIES,
                                     context->response.GetCookies());

    napi_value header = MakeResponseHeader(context);
    if (NapiUtils::GetValueType(context->GetEnv(), header) == napi_object) {
        OnHeaderReceive(context, header);
        NapiUtils::SetNamedProperty(context->GetEnv(), object, HttpConstant::RESPONSE_KEY_HEADER, header);
    }

    if (context->options.GetHttpDataType() != HttpDataType::NO_DATA_TYPE && ProcByExpectDataType(object, context)) {
        return object;
    }

    auto contentType = CommonUtils::ToLower(const_cast<std::map<std::string, std::string> &>(
        context->response.GetHeader())[HttpConstant::HTTP_CONTENT_TYPE]);
    if (contentType.find(HttpConstant::HTTP_CONTENT_TYPE_OCTET_STREAM) != std::string::npos ||
        contentType.find(HttpConstant::HTTP_CONTENT_TYPE_IMAGE) != std::string::npos) {
        void *data = nullptr;
        auto body = context->response.GetResult();
        napi_value arrayBuffer = NapiUtils::CreateArrayBuffer(context->GetEnv(), body.size(), &data);
        if (data != nullptr && arrayBuffer != nullptr) {
            if (memcpy_s(data, body.size(), body.c_str(), body.size()) != EOK) {
                NETSTACK_LOGE("memcpy_s failed!");
                return object;
            }
            NapiUtils::SetNamedProperty(context->GetEnv(), object, HttpConstant::RESPONSE_KEY_RESULT, arrayBuffer);
        }
        NapiUtils::SetUint32Property(context->GetEnv(), object, HttpConstant::RESPONSE_KEY_RESULT_TYPE,
                                     static_cast<uint32_t>(HttpDataType::ARRAY_BUFFER));
        return object;
    }

    /* now just support utf8 */
    NapiUtils::SetStringPropertyUtf8(context->GetEnv(), object, HttpConstant::RESPONSE_KEY_RESULT,
                                     context->response.GetResult());
    NapiUtils::SetUint32Property(context->GetEnv(), object, HttpConstant::RESPONSE_KEY_RESULT_TYPE,
                                 static_cast<uint32_t>(HttpDataType::STRING));
    return object;
}

std::string HttpExec::MakeUrl(const std::string &url, std::string param, const std::string &extraParam)
{
    if (param.empty()) {
        param += extraParam;
    } else {
        param += HttpConstant::HTTP_URL_PARAM_SEPARATOR;
        param += extraParam;
    }

    if (param.empty()) {
        return url;
    }

    return url + HttpConstant::HTTP_URL_PARAM_START + param;
}

bool HttpExec::MethodForGet(const std::string &method)
{
    return (method == HttpConstant::HTTP_METHOD_HEAD || method == HttpConstant::HTTP_METHOD_OPTIONS ||
            method == HttpConstant::HTTP_METHOD_DELETE || method == HttpConstant::HTTP_METHOD_TRACE ||
            method == HttpConstant::HTTP_METHOD_GET || method == HttpConstant::HTTP_METHOD_CONNECT);
}

bool HttpExec::MethodForPost(const std::string &method)
{
    return (method == HttpConstant::HTTP_METHOD_POST || method == HttpConstant::HTTP_METHOD_PUT);
}

bool HttpExec::EncodeUrlParam(std::string &str)
{
    char encoded[4];
    std::string encodeOut;
    size_t length = strlen(str.c_str());
    for (size_t i = 0; i < length; ++i) {
        auto c = static_cast<uint8_t>(str.c_str()[i]);
        if (IsUnReserved(c)) {
            encodeOut += static_cast<char>(c);
        } else {
            if (sprintf_s(encoded, sizeof(encoded), "%%%02X", c) < 0) {
                return false;
            }
            encodeOut += encoded;
        }
    }

    if (str == encodeOut) {
        return false;
    }
    str = encodeOut;
    return true;
}

bool HttpExec::Initialize()
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (initialized_) {
        return true;
    }
    NETSTACK_LOGI("call curl_global_init");
    if (curl_global_init(CURL_GLOBAL_ALL) != CURLE_OK) {
        NETSTACK_LOGE("Failed to initialize 'curl'");
        return false;
    }

    curlMulti_ = curl_multi_init();
    if (curlMulti_ == nullptr) {
        NETSTACK_LOGE("Failed to initialize 'curl_multi'");
        return false;
    }

    std::thread([] {
        while (true) {
            {
                std::lock_guard guard(curlMultiMutex_);
                int runningHandle;
                auto code = curl_multi_perform(curlMulti_, &runningHandle);
                if (code == CURLM_OK) {
                    int leftMsg;
                    CURLMsg *msg = curl_multi_info_read(curlMulti_, &leftMsg);
                    if (msg != nullptr) {
                        HttpExec::HandleCurlData(msg);
                        if (msg->easy_handle != nullptr) {
                            (void)curl_multi_remove_handle(curlMulti_, msg->easy_handle);
                            (void)curl_easy_cleanup(msg->easy_handle);
                        }
                    }
                }
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(CURL_TIMEOUT_MS));
        }
    }).detach();

    initialized_ = true;
    return initialized_;
}

bool HttpExec::SetOption(CURL *curl, RequestContext *context, struct curl_slist *requestHeader)
{
    const std::string &method = context->options.GetMethod();
    if (!MethodForGet(method) && !MethodForPost(method)) {
        NETSTACK_LOGE("method %{public}s not supported", method.c_str());
        return false;
    }

    if (context->options.GetMethod() == HttpConstant::HTTP_METHOD_HEAD) {
        NETSTACK_CURL_EASY_SET_OPTION(curl, CURLOPT_NOBODY, 1L, context);
    }

    NETSTACK_CURL_EASY_SET_OPTION(curl, CURLOPT_URL, context->options.GetUrl().c_str(), context);
    NETSTACK_CURL_EASY_SET_OPTION(curl, CURLOPT_CUSTOMREQUEST, method.c_str(), context);

    NETSTACK_CURL_EASY_SET_OPTION(curl, CURLOPT_WRITEFUNCTION, OnWritingMemoryBody, context);
    NETSTACK_CURL_EASY_SET_OPTION(curl, CURLOPT_WRITEDATA, context, context);

    NETSTACK_CURL_EASY_SET_OPTION(curl, CURLOPT_HEADERFUNCTION, OnWritingMemoryHeader, context);
    NETSTACK_CURL_EASY_SET_OPTION(curl, CURLOPT_HEADERDATA, context, context);

    NETSTACK_CURL_EASY_SET_OPTION(curl, CURLOPT_HTTPHEADER, requestHeader, context);

    // Some servers don't like requests that are made without a user-agent field, so we provide one
    NETSTACK_CURL_EASY_SET_OPTION(curl, CURLOPT_USERAGENT, HttpConstant::HTTP_DEFAULT_USER_AGENT, context);

    NETSTACK_CURL_EASY_SET_OPTION(curl, CURLOPT_FOLLOWLOCATION, 1L, context);

    /* first #undef CURL_DISABLE_COOKIES in curl config */
    NETSTACK_CURL_EASY_SET_OPTION(curl, CURLOPT_COOKIEFILE, "", context);

#if NETSTACK_USE_PROXY
    NETSTACK_CURL_EASY_SET_OPTION(curl, CURLOPT_PROXY, NETSTACK_PROXY_URL_PORT, context);
    NETSTACK_CURL_EASY_SET_OPTION(curl, CURLOPT_PROXYTYPE, NETSTACK_PROXY_TYPE, context);
    NETSTACK_CURL_EASY_SET_OPTION(curl, CURLOPT_HTTPPROXYTUNNEL, 1L, context);
#ifdef NETSTACK_PROXY_PASS
    NETSTACK_CURL_EASY_SET_OPTION(curl, CURLOPT_PROXYUSERPWD, NETSTACK_PROXY_PASS, context);
#endif // NETSTACK_PROXY_PASS
#endif // NETSTACK_USE_PROXY

#if NO_SSL_CERTIFICATION
    // in real life, you should buy a ssl certification and rename it to /etc/ssl/cert.pem
    NETSTACK_CURL_EASY_SET_OPTION(curl, CURLOPT_SSL_VERIFYHOST, 0L, context);
    NETSTACK_CURL_EASY_SET_OPTION(curl, CURLOPT_SSL_VERIFYPEER, 0L, context);
#else
#ifndef WINDOWS_PLATFORM
    NETSTACK_CURL_EASY_SET_OPTION(curl, CURLOPT_CAINFO, HttpConstant::HTTP_DEFAULT_CA_PATH, context);
#endif // WINDOWS_PLATFORM
#endif // NO_SSL_CERTIFICATION

    NETSTACK_CURL_EASY_SET_OPTION(curl, CURLOPT_NOPROGRESS, 1L, context);
    NETSTACK_CURL_EASY_SET_OPTION(curl, CURLOPT_NOSIGNAL, 1L, context);
#if HTTP_CURL_PRINT_VERBOSE
    NETSTACK_CURL_EASY_SET_OPTION(curl, CURLOPT_VERBOSE, 1L, context);
#endif
    NETSTACK_CURL_EASY_SET_OPTION(curl, CURLOPT_TIMEOUT_MS, context->options.GetReadTimeout(), context);
    NETSTACK_CURL_EASY_SET_OPTION(curl, CURLOPT_CONNECTTIMEOUT_MS, context->options.GetConnectTimeout(), context);
#ifndef WINDOWS_PLATFORM
    NETSTACK_CURL_EASY_SET_OPTION(curl, CURLOPT_ACCEPT_ENCODING, HttpConstant::HTTP_CONTENT_ENCODING_GZIP, context);
#endif

    if (MethodForPost(method) && !context->options.GetBody().empty()) {
        NETSTACK_CURL_EASY_SET_OPTION(curl, CURLOPT_POST, 1L, context);
        NETSTACK_CURL_EASY_SET_OPTION(curl, CURLOPT_POSTFIELDS, context->options.GetBody().c_str(), context);
        NETSTACK_CURL_EASY_SET_OPTION(curl, CURLOPT_POSTFIELDSIZE, context->options.GetBody().size(), context);
    }

    NETSTACK_CURL_EASY_SET_OPTION(curl, CURLOPT_HTTP_VERSION, context->options.GetHttpVersion(), context);

    return true;
}

size_t HttpExec::OnWritingMemoryBody(const void *data, size_t size, size_t memBytes, void *userData)
{
    auto context = static_cast<RequestContext *>(userData);
    context->response.AppendResult(data, size * memBytes);
    return size * memBytes;
}

size_t HttpExec::OnWritingMemoryHeader(const void *data, size_t size, size_t memBytes, void *userData)
{
    auto context = static_cast<RequestContext *>(userData);
    if (context->response.GetResult().size() > MAX_LIMIT) {
        return 0;
    }
    context->response.AppendRawHeader(data, size * memBytes);
    return size * memBytes;
}

struct curl_slist *HttpExec::MakeHeaders(const std::vector<std::string> &vec)
{
    struct curl_slist *header = nullptr;
    std::for_each(vec.begin(), vec.end(), [&header](const std::string &s) {
        if (!s.empty()) {
            header = curl_slist_append(header, s.c_str());
        }
    });
    return header;
}

napi_value HttpExec::MakeResponseHeader(RequestContext *context)
{
    napi_value header = NapiUtils::CreateObject(context->GetEnv());
    if (NapiUtils::GetValueType(context->GetEnv(), header) == napi_object) {
        std::for_each(context->response.GetHeader().begin(), context->response.GetHeader().end(),
                      [context, header](const std::pair<std::string, std::string> &p) {
                          if (!p.first.empty() && !p.second.empty()) {
                              NapiUtils::SetStringPropertyUtf8(context->GetEnv(), header, p.first, p.second);
                          }
                      });
    }
    return header;
}

void HttpExec::OnHeaderReceive(RequestContext *context, napi_value header)
{
    napi_value undefined = NapiUtils::GetUndefined(context->GetEnv());
    context->Emit(ON_HEADER_RECEIVE, std::make_pair(undefined, header));
    context->Emit(ON_HEADERS_RECEIVE, std::make_pair(undefined, header));
}

bool HttpExec::IsUnReserved(unsigned char in)
{
    if ((in >= '0' && in <= '9') || (in >= 'a' && in <= 'z') || (in >= 'A' && in <= 'Z')) {
        return true;
    }
    switch (in) {
        case '-':
        case '.':
        case '_':
        case '~':
            return true;
        default:
            break;
    }
    return false;
}

bool HttpExec::ProcByExpectDataType(napi_value object, RequestContext *context)
{
    switch (context->options.GetHttpDataType()) {
        case HttpDataType::STRING: {
            NapiUtils::SetStringPropertyUtf8(context->GetEnv(), object, HttpConstant::RESPONSE_KEY_RESULT,
                                             context->response.GetResult());
            NapiUtils::SetUint32Property(context->GetEnv(), object, HttpConstant::RESPONSE_KEY_RESULT_TYPE,
                                         static_cast<uint32_t>(HttpDataType::STRING));
            return true;
        }
        case HttpDataType::OBJECT: {
            if (context->response.GetResult().size() > HttpConstant::MAX_JSON_PARSE_SIZE) {
                return false;
            }

            napi_value obj = NapiUtils::JsonParse(
                context->GetEnv(), NapiUtils::CreateStringUtf8(context->GetEnv(), context->response.GetResult()));
            if (obj) {
                NapiUtils::SetNamedProperty(context->GetEnv(), object, HttpConstant::RESPONSE_KEY_RESULT, obj);
                NapiUtils::SetUint32Property(context->GetEnv(), object, HttpConstant::RESPONSE_KEY_RESULT_TYPE,
                                             static_cast<uint32_t>(HttpDataType::OBJECT));
                return true;
            }

            // parse maybe failed
            return false;
        }
        case HttpDataType::ARRAY_BUFFER: {
            void *data = nullptr;
            auto body = context->response.GetResult();
            napi_value arrayBuffer = NapiUtils::CreateArrayBuffer(context->GetEnv(), body.size(), &data);
            if (data != nullptr && arrayBuffer != nullptr) {
                if (memcpy_s(data, body.size(), body.c_str(), body.size()) < 0) {
                    NETSTACK_LOGE("[ProcByExpectDataType] memory copy failed");
                    return true;
                }
                NapiUtils::SetNamedProperty(context->GetEnv(), object, HttpConstant::RESPONSE_KEY_RESULT, arrayBuffer);
                NapiUtils::SetUint32Property(context->GetEnv(), object, HttpConstant::RESPONSE_KEY_RESULT_TYPE,
                                             static_cast<uint32_t>(HttpDataType::ARRAY_BUFFER));
            }
            return true;
        }
        default:
            break;
    }
    return false;
}

#ifndef MAC_PLATFORM
void HttpExec::AsyncRunRequest(RequestContext *context)
{
    HttpAsyncWork::ExecRequest(context->GetEnv(), context);
}
#endif

bool HttpResponseCacheExec::ExecFlush(BaseContext *context)
{
    (void)context;
    CacheProxy::FlushCache();
    return true;
}

napi_value HttpResponseCacheExec::FlushCallback(BaseContext *context)
{
    return NapiUtils::GetUndefined(context->GetEnv());
}

bool HttpResponseCacheExec::ExecDelete(BaseContext *context)
{
    (void)context;
    CacheProxy::StopCacheAndDelete();
    return true;
}

napi_value HttpResponseCacheExec::DeleteCallback(BaseContext *context)
{
    return NapiUtils::GetUndefined(context->GetEnv());
}
} // namespace OHOS::NetStack
