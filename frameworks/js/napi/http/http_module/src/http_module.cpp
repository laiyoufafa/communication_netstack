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

#include "http_module.h"

#include "constant.h"
#include "event_list.h"
#include "http_async_work.h"
#include "http_exec.h"
#include "netstack_log.h"
#include "netstack_module_template.h"

#define DECLARE_RESPONSE_CODE(code) \
    DECLARE_NAPI_STATIC_PROPERTY(#code, NapiUtils::CreateUint32(env, static_cast<uint32_t>(ResponseCode::code)))

#define DECLARE_REQUEST_METHOD(method) \
    DECLARE_NAPI_STATIC_PROPERTY(HttpConstant::method, NapiUtils::CreateStringUtf8(env, HttpConstant::method))

static constexpr const char *REQUEST_ASYNC_WORK_NAME = "ExecRequest";

static constexpr const char *HTTP_MODULE_NAME = "net.http";

namespace OHOS::NetStack {
napi_value HttpModuleExports::InitHttpModule(napi_env env, napi_value exports)
{
    DefineHttpRequestClass(env, exports);
    InitHttpProperties(env, exports);

    return exports;
}

napi_value HttpModuleExports::CreateHttp(napi_env env, napi_callback_info info)
{
    return ModuleTemplate::NewInstance(env, info, INTERFACE_HTTP_REQUEST, [](napi_env, void *data, void *) {
        NETSTACK_LOGI("http request handle is finalized");
        (void)data;
    });
}

void HttpModuleExports::DefineHttpRequestClass(napi_env env, napi_value exports)
{
    std::initializer_list<napi_property_descriptor> properties = {
        DECLARE_NAPI_FUNCTION(HttpRequest::FUNCTION_REQUEST, HttpRequest::Request),
        DECLARE_NAPI_FUNCTION(HttpRequest::FUNCTION_DESTROY, HttpRequest::Destroy),
        DECLARE_NAPI_FUNCTION(HttpRequest::FUNCTION_ON, HttpRequest::On),
        DECLARE_NAPI_FUNCTION(HttpRequest::FUNCTION_ONCE, HttpRequest::Once),
        DECLARE_NAPI_FUNCTION(HttpRequest::FUNCTION_OFF, HttpRequest::Off),
    };
    ModuleTemplate::DefineClass(env, exports, properties, INTERFACE_HTTP_REQUEST);
}

void HttpModuleExports::InitHttpProperties(napi_env env, napi_value exports)
{
    std::initializer_list<napi_property_descriptor> properties = {
        DECLARE_NAPI_FUNCTION(FUNCTION_CREATE_HTTP, CreateHttp),
    };
    NapiUtils::DefineProperties(env, exports, properties);

    InitRequestMethod(env, exports);
    InitResponseCode(env, exports);
}

void HttpModuleExports::InitRequestMethod(napi_env env, napi_value exports)
{
    std::initializer_list<napi_property_descriptor> properties = {
        DECLARE_REQUEST_METHOD(HTTP_METHOD_OPTIONS), DECLARE_REQUEST_METHOD(HTTP_METHOD_GET),
        DECLARE_REQUEST_METHOD(HTTP_METHOD_HEAD),    DECLARE_REQUEST_METHOD(HTTP_METHOD_POST),
        DECLARE_REQUEST_METHOD(HTTP_METHOD_PUT),     DECLARE_REQUEST_METHOD(HTTP_METHOD_DELETE),
        DECLARE_REQUEST_METHOD(HTTP_METHOD_TRACE),   DECLARE_REQUEST_METHOD(HTTP_METHOD_CONNECT),
    };

    napi_value requestMethod = NapiUtils::CreateObject(env);
    NapiUtils::DefineProperties(env, requestMethod, properties);

    NapiUtils::SetNamedProperty(env, exports, INTERFACE_REQUEST_METHOD, requestMethod);
}

void HttpModuleExports::InitResponseCode(napi_env env, napi_value exports)
{
    std::initializer_list<napi_property_descriptor> properties = {
        DECLARE_RESPONSE_CODE(OK),
        DECLARE_RESPONSE_CODE(CREATED),
        DECLARE_RESPONSE_CODE(ACCEPTED),
        DECLARE_RESPONSE_CODE(NOT_AUTHORITATIVE),
        DECLARE_RESPONSE_CODE(NO_CONTENT),
        DECLARE_RESPONSE_CODE(RESET),
        DECLARE_RESPONSE_CODE(PARTIAL),
        DECLARE_RESPONSE_CODE(MULT_CHOICE),
        DECLARE_RESPONSE_CODE(MOVED_PERM),
        DECLARE_RESPONSE_CODE(MOVED_TEMP),
        DECLARE_RESPONSE_CODE(SEE_OTHER),
        DECLARE_RESPONSE_CODE(NOT_MODIFIED),
        DECLARE_RESPONSE_CODE(USE_PROXY),
        DECLARE_RESPONSE_CODE(BAD_REQUEST),
        DECLARE_RESPONSE_CODE(UNAUTHORIZED),
        DECLARE_RESPONSE_CODE(PAYMENT_REQUIRED),
        DECLARE_RESPONSE_CODE(FORBIDDEN),
        DECLARE_RESPONSE_CODE(NOT_FOUND),
        DECLARE_RESPONSE_CODE(BAD_METHOD),
        DECLARE_RESPONSE_CODE(NOT_ACCEPTABLE),
        DECLARE_RESPONSE_CODE(PROXY_AUTH),
        DECLARE_RESPONSE_CODE(CLIENT_TIMEOUT),
        DECLARE_RESPONSE_CODE(CONFLICT),
        DECLARE_RESPONSE_CODE(GONE),
        DECLARE_RESPONSE_CODE(LENGTH_REQUIRED),
        DECLARE_RESPONSE_CODE(PRECON_FAILED),
        DECLARE_RESPONSE_CODE(ENTITY_TOO_LARGE),
        DECLARE_RESPONSE_CODE(REQ_TOO_LONG),
        DECLARE_RESPONSE_CODE(UNSUPPORTED_TYPE),
        DECLARE_RESPONSE_CODE(INTERNAL_ERROR),
        DECLARE_RESPONSE_CODE(NOT_IMPLEMENTED),
        DECLARE_RESPONSE_CODE(BAD_GATEWAY),
        DECLARE_RESPONSE_CODE(UNAVAILABLE),
        DECLARE_RESPONSE_CODE(GATEWAY_TIMEOUT),
        DECLARE_RESPONSE_CODE(VERSION),
    };

    napi_value responseCode = NapiUtils::CreateObject(env);
    NapiUtils::DefineProperties(env, responseCode, properties);

    NapiUtils::SetNamedProperty(env, exports, INTERFACE_RESPONSE_CODE, responseCode);
}

napi_value HttpModuleExports::HttpRequest::Request(napi_env env, napi_callback_info info)
{
    return ModuleTemplate::Interface<RequestContext>(
        env, info, REQUEST_ASYNC_WORK_NAME,
        [](napi_env, napi_value, RequestContext *) -> bool { return HttpExec::Initialize(); },
        HttpAsyncWork::ExecRequest, HttpAsyncWork::RequestCallback);
}

napi_value HttpModuleExports::HttpRequest::Destroy(napi_env env, napi_callback_info info)
{
    (void)env;
    (void)info;

    return NapiUtils::GetUndefined(env);
}

napi_value HttpModuleExports::HttpRequest::On(napi_env env, napi_callback_info info)
{
    ModuleTemplate::On(env, info, {ON_HEADERS_RECEIVE}, false);
    return ModuleTemplate::On(env, info, {ON_HEADER_RECEIVE}, true);
}

napi_value HttpModuleExports::HttpRequest::Once(napi_env env, napi_callback_info info)
{
    return ModuleTemplate::Once(env, info, {ON_HEADER_RECEIVE, ON_HEADERS_RECEIVE}, false);
}

napi_value HttpModuleExports::HttpRequest::Off(napi_env env, napi_callback_info info)
{
    ModuleTemplate::Off(env, info, {ON_HEADERS_RECEIVE});
    return ModuleTemplate::Off(env, info, {ON_HEADER_RECEIVE});
}

static napi_module g_httpModule = {
    .nm_version = 1,
    .nm_flags = 0,
    .nm_filename = nullptr,
    .nm_register_func = HttpModuleExports::InitHttpModule,
    .nm_modname = HTTP_MODULE_NAME,
    .nm_priv = nullptr,
    .reserved = {nullptr},
};

extern "C" __attribute__((constructor)) void RegisterHttpModule(void)
{
    napi_module_register(&g_httpModule);
}
} // namespace OHOS::NetStack
