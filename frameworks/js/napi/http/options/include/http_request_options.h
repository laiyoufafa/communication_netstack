/*
 * Copyright (C) 2021-2022 Huawei Device Co., Ltd.
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

#ifndef COMMUNICATIONNETSTACK_HTTP_REQUEST_OPTIONS_H
#define COMMUNICATIONNETSTACK_HTTP_REQUEST_OPTIONS_H

#include <map>
#include <string>

#include "constant.h"

namespace OHOS::NetStack {
enum class HttpProtocol {
    HTTP1_1,
    HTTP2,
    HTTP_NONE, // default choose by curl
};

class HttpRequestOptions final {
public:
    HttpRequestOptions();

    void SetUrl(const std::string &url);

    void SetMethod(const std::string &method);

    void SetBody(const void *data, size_t length);

    void SetHeader(const std::string &key, const std::string &val);

    void SetReadTimeout(uint32_t readTimeout);

    void SetConnectTimeout(uint32_t connectTimeout);

    void SetUsingProtocol(HttpProtocol httpProtocol);

    void SetHttpDataType(HttpDataType dataType);

    [[nodiscard]] const std::string &GetUrl() const;

    [[nodiscard]] const std::string &GetMethod() const;

    [[nodiscard]] const std::string &GetBody() const;

    [[nodiscard]] const std::map<std::string, std::string> &GetHeader() const;

    [[nodiscard]] uint32_t GetReadTimeout() const;

    [[nodiscard]] uint32_t GetConnectTimeout() const;

    [[nodiscard]] uint32_t GetHttpVersion() const;

    void SetRequestTime(const std::string &time);

    [[nodiscard]] const std::string &GetRequestTime() const;

    [[nodiscard]] HttpDataType GetHttpDataType() const;

    void SetPriority(uint32_t priority);

    [[nodiscard]] uint32_t GetPriority() const;

private:
    std::string url_;

    std::string body_;

    std::string method_;

    std::map<std::string, std::string> header_;

    uint32_t readTimeout_;

    uint32_t connectTimeout_;

    HttpProtocol usingProtocol_;

    std::string requestTime_;

    HttpDataType dataType_;

    uint32_t priority_;
};
} // namespace OHOS::NetStack

#endif /* COMMUNICATIONNETSTACK_HTTP_REQUEST_OPTIONS_H */
