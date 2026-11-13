#pragma once

#include "ai/DataPolicy.h"
#include "ai/ProviderConfig.h"

#include <QString>
#include <QVector>

namespace aipdf::ai {

// 一次云端请求的「将要发送什么」。
//
// 这个结构是上传预览与审计的共同数据源：界面把它念给用户听，
// 审计把它记成元数据。两者都只看到数量与页码，看不到正文。
struct UploadPreview {
    DataMode mode = DataMode::SelectedChunks;
    QString host;         // 目标主机，例如 api.openai.com
    int chunkCount = 0;
    int charCount = 0;
    QVector<int> pages;   // 1 起的页号，去重升序
    PolicyDecision decision;
};

// 从 baseUrl 里取主机名；解析失败返回空串。
QString hostFromBaseUrl(const QString& baseUrl);

// 组装预览并跑一次策略判定。
UploadPreview buildUploadPreview(const ProviderConfig& config,
                                 const DataScope& scope,
                                 QVector<int> pages);

}  // namespace aipdf::ai
