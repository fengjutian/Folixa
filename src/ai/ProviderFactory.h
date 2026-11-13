#pragma once

#include "ai/ModelProvider.h"
#include "ai/ProviderConfig.h"

#include <memory>

namespace aipdf::ai {

// 按配置造一个 Provider。
//
// 规则很简单：**配了密钥就用真的，没配就用演示模式**。
// 没有密钥时不能报错退出——那样用户第一次打开应用就看到一个坏掉的 AI 面板。
// 但演示模式必须在界面上明确标出来，不能让人以为模型真的回答了。
std::unique_ptr<ModelProvider> createProvider(const ProviderConfig& config);

}  // namespace aipdf::ai
