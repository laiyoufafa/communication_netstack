/*
 * Copyright (c) 2022 Huawei Device Co., Ltd.
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

#include "connect_context.h"

#include "constant.h"
#include "netstack_common_utils.h"
#include "netstack_log.h"
#include "netstack_napi_utils.h"
#include "securec.h"

namespace OHOS::NetStack {
ConnectContext::ConnectContext(napi_env env, EventManager *manager) : BaseContext(env, manager) {}

ConnectContext::~ConnectContext() = default;

void ConnectContext::ParseParams(napi_value *params, size_t paramsCount)
{
    if (!CheckParamsType(params, paramsCount)) {
        NETSTACK_LOGE("ConnectContext Parse Failed");
        return;
    }

    url = NapiUtils::GetStringFromValueUtf8(GetEnv(), params[0]);
    if (paramsCount == FUNCTION_PARAM_ONE) {
        NETSTACK_LOGI("ConnectContext paramsCount == FUNCTION_PARAM_ONE");
        return SetParseOK(true);
    }

    if (NapiUtils::GetValueType(GetEnv(), params[1]) == napi_function) {
        NETSTACK_LOGI("ConnectContext NapiUtils::GetValueType(GetEnv(), params[1]) == napi_function");
        return SetParseOK(SetCallback(params[1]) == napi_ok);
    }

    NETSTACK_LOGI("ConnectContext NapiUtils::GetValueType(GetEnv(), params[1]) == napi_object");
    ParseHeader(params[1]);

    if (paramsCount == FUNCTION_PARAM_THREE) {
        NETSTACK_LOGI("ConnectContext paramsCount == FUNCTION_PARAM_THREE");
        return SetParseOK(SetCallback(params[FUNCTION_PARAM_TWO]) == napi_ok);
    }
    return SetParseOK(true);
}

void ConnectContext::ParseHeader(napi_value optionsValue)
{
    if (!NapiUtils::HasNamedProperty(GetEnv(), optionsValue, ContextKey::HEADER)) {
        return;
    }
    napi_value jsHeader = NapiUtils::GetNamedProperty(GetEnv(), optionsValue, ContextKey::HEADER);
    if (NapiUtils::GetValueType(GetEnv(), jsHeader) != napi_object) {
        return;
    }
    auto names = NapiUtils::GetPropertyNames(GetEnv(), jsHeader);
    std::for_each(names.begin(), names.end(), [jsHeader, this](const std::string &name) {
        auto value = NapiUtils::GetStringPropertyUtf8(GetEnv(), jsHeader, name);
        if (!value.empty()) {
            // header key ignores key but value not
            header[CommonUtils::ToLower(name)] = value;
        }
    });
}

bool ConnectContext::CheckParamsType(napi_value *params, size_t paramsCount)
{
    if (paramsCount == FUNCTION_PARAM_ONE) {
        return NapiUtils::GetValueType(GetEnv(), params[0]) == napi_string;
    }

    if (paramsCount == FUNCTION_PARAM_TWO) {
        return NapiUtils::GetValueType(GetEnv(), params[0]) == napi_string &&
               (NapiUtils::GetValueType(GetEnv(), params[1]) == napi_function ||
                NapiUtils::GetValueType(GetEnv(), params[1]) == napi_object);
    }

    if (paramsCount == FUNCTION_PARAM_THREE) {
        return NapiUtils::GetValueType(GetEnv(), params[0]) == napi_string &&
               NapiUtils::GetValueType(GetEnv(), params[1]) == napi_object &&
               NapiUtils::GetValueType(GetEnv(), params[FUNCTION_PARAM_TWO]) == napi_function;
    }

    return false;
}
} // namespace OHOS::NetStack
