#pragma once

#include <QByteArray>
#include <QString>

namespace aipdf::ai {

// AI 内容检测的结果。
//
// 为什么「解析不出来」不等于「不是 AI 写的」：检测给的是一个**判定**，
// 不是二值开关。模型没给出可解析的结论时，界面必须显示「无法判断」，
// 而不是显示「完全人工」——后者会被当成一个确定结论，用户据此下判断就会出错。
struct DetectionVerdict {
    // 是否拿到了一个可用的判定。
    // false 时 category 固定是「无法判断」，reason 里写中文原因。
    bool ok = false;
    // 判定结果：是否由 AI 生成（或经 AI 改写）。
    bool likelyAiGenerated = false;
    // 置信度（0..100）。
    int confidence = 0;
    // 判定档位：「完全人工」/「疑似 AI 生成」/「疑似 AI 改写」/「无法判断」。
    QString category;
    // 中文理由：成功时是模型给的理由，失败时是本层给出的中文原因。
    QString reason;
    // 模型原始输出：解析失败时也要留着，便于排查模型到底返回了什么。
    QString rawResponse;
};

// 构造检测请求的提示词（里面含待检文本）。
//
// 返回 UTF-8 字节而不是 QString：提示词最终要进 JSON 请求体，
// 字节形式可以原样拼进去，调用方不必再自己做一次编码转换。
//
// text 为空或只有空白时返回空数组，并把中文原因写进 error（error 可以为空指针）。
QByteArray buildDetectionPrompt(const QString& text, QString* error = nullptr);

// 解析模型返回的 JSON。
//
// 支持两种形状：
//   {"ai_probability": 0.82, "reason": "…", "category": "疑似 AI 生成"}
//   {"likely_ai": true, "confidence": 75, "reason": "…"}
//
// 两种形状同时出现时以布尔结论为准（它比概率更接近模型自己的判断）。
// 解析不出判定字段、或模型没给理由，一律 ok=false——绝不退化成「不是 AI 写的」。
DetectionVerdict parseDetectionResponse(const QByteArray& jsonBytes);

// 判定档位：把概率映射成三档。
//
//   >= 0.75  疑似 AI 生成
//   >= 0.50  疑似 AI 改写
//   <  0.50  完全人工
//
// 0.5 这个边界归到「疑似」那一侧：漏判（把 AI 写的判成人写的）代价更高。
QString detectionCategory(double aiProbability);

}  // namespace aipdf::ai
