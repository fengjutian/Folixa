#pragma once
#include "pdf/kernel/Navigation.h"
#include <QByteArray>
#include <QString>
#include <vector>
namespace aipdf::pdf {
struct OutlineWriteResult { QByteArray bytes; bool ok = false; QString error; };
OutlineWriteResult rewriteOutline(const QByteArray& pdf,
                                  const std::vector<OutlineItem>& outline);
}  // namespace aipdf::pdf
