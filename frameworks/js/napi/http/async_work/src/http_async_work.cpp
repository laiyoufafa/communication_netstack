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

#include "http_async_work.h"

#include "http_exec.h"
#include "netstack_base_async_work.h"
#include "netstack_napi_utils.h"

namespace OHOS::NetStack {
void HttpAsyncWork::ExecRequest(napi_env env, void *data)
{
    BaseAsyncWork::ExecAsyncWork<RequestContext, HttpExec::ExecRequest>(env, data);
}

void HttpAsyncWork::RequestCallback(napi_env env, napi_status status, void *data)
{
    BaseAsyncWork::AsyncWorkCallback<RequestContext, HttpExec::RequestCallback>(env, status, data);
}
} // namespace OHOS::NetStack