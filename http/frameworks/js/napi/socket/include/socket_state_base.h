/*
 * Copyright (C) 2021 Huawei Device Co., Ltd.
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

#ifndef SOCKET_BASE_CONTEXT_H
#define SOCKET_BASE_CONTEXT_H

#include "napi/native_api.h"
#include "napi/native_node_api.h"
#include "napi_util.h"

namespace OHOS {
namespace NetManagerStandard {
class SocketStateBase {
public:
    SocketStateBase() {}
    ~SocketStateBase() {}

    bool GetBound()
    {
        return isBound_;
    }

    void SetBound(bool bound)
    {
        this->isBound_ = bound;
    }

    bool GetClose()
    {
        return isClose_;
    }

    void SetClose(bool close)
    {
        this->isClose_ = close;
    }

    bool GetConnected()
    {
        return isConnected_;
    }

    void SetConnected(bool connected)
    {
        this->isConnected_ = connected;
    }
private:
    bool isBound_ = false;
    bool isClose_ = false;
    bool isConnected_ = false;
};
} // namespace NetManagerStandard
} // namespace OHOS
#endif // SOCKET_BASE_CONTEXT_H