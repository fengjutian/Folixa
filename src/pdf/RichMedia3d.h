#pragma once

#include <QByteArray>
#include <QColor>
#include <QRectF>
#include <QString>
#include <QStringList>
#include <QVector>

namespace aipdf::pdf {

// ============================================================================
// 这个模块做什么、不做什么
// ============================================================================
//
// 做：按 PDF 规范（ISO 32000-1 §13.6「3D 艺术品」与 §13.7「富媒体」）
//     把 3D 注解（/Subtype /3D）与多媒体注解（/Subtype /RichMedia）的
//     字典、外观流、文件说明与资源写进 PDF，并把它们原样读回来。
//
// 不做——这是**取舍**，不是缺陷：
//   * **不渲染 3D**。本程序没有 U3D 运行时，写进去的模型在界面上不会动。
//   * **不解码音视频**。本程序没有音频/视频解码器，多媒体注解在界面上不播放。
//   * **不解析 U3D 的 PRC 内容**。PRC（Portable Reality Container）是 ECMA-363
//     定义的二进制容器，本程序**原样搬运**调用方给的字节，既不校验也不改写；
//     模型能不能被别的阅读器渲染出来，取决于那份字节本身合不合法。
//   * **降级图像不是 /3DB**。按 ISO 32000-1，/3DB 是「3D 视图框」
//     （模型在注解里占多大地方），而「阅读器不支持 3D 时显示什么」是注解的
//     普通外观 /AP /N。本程序按规范写：/3DB 写视图框，降级图像写成 /AP /N
//     里的一张图像。
//
// 由此，验收标准是「结构写对不对、读不读得回来、别的阅读器认不认得」，
// 而不是「画面好不好看」。写进去的 3D 与多媒体注解在**具备相应运行时的
// 阅读器**（Acrobat 等）里才会动、才会播；本程序只保证文件结构是对的。
//
// 关于「作品集」：作品集（PDF 包）**不在本模块里重做**。仓库已有
// pdf::writePortfolio / pdf::readPortfolio（src/pdf/PortfolioWriter.h），
// 作品集就是「一个 PDF 里装着若干附件 + 一组封面页」，由那两个函数负责。
// 本模块只负责 3D 与多媒体注解，两者互补而不重叠。

// ============================================================================
// 3D
// ============================================================================

// 3D 视图参数。
//
// 对应 PDF 里的 3D 视图字典：注解上的 /3DV，以及 3D 流（/3DD）里 /VA 数组的元素。
// 名字里的前缀斜杠是照抄 PDF 的写法（视图字典里的值是「名字对象」），
// 调用方传 "Realistic" 或 "/Realistic" 都认。
struct ThreeDViewSpec {
    // 视图的对外名字（/XN）：给人看的，会出现在阅读器的视图列表里。
    QString externalName = QStringLiteral("Default");
    // 视图的内部名字（/IN）：给脚本用的。
    QString internalName = QStringLiteral("Default");

    // 相机到世界的变换矩阵（/C2W）：12 个数，按 PDF 的 4x4 矩阵顺序展开。
    // 为空表示不写这一项——阅读器会用模型自带的世界变换。
    QVector<double> cameraToWorld;

    // 相机到轨道中心的距离（/CO）：绕着模型转时的「半径」。
    bool hasOrbitDistance = false;
    double orbitDistance = 0.0;

    // 投影方式（/P 字典的 /P）：/Perspective（透视）或 /Orthographic（正交）。
    QString projectionType = QStringLiteral("/Perspective");
    // 视场角（/Fov，单位：度）。只在透视投影下有意义。
    double fieldOfView = 45.0;

    // 背景色（/BG 的 /C）：DeviceRGB 三元组。
    bool hasBackground = true;
    QColor background = QColor(Qt::white);
    // 背景是否画阴影（/BG 的 /S）、是否受光照（/BG 的 /I）。
    bool backgroundShadow = false;
    bool backgroundIlluminated = true;

    // 渲染风格（/RM 的 /RS）：/Realistic、/Shaded、/HiddenLine、/BoundingBox。
    QString renderStyle = QStringLiteral("/Realistic");
    // 渲染不透明度（/RM 的 /O），取值 0..1。
    double renderOpacity = 1.0;

    // 光照方案（/LS 的 /Subtype）：/Artwork、/None、/White、/Day、/Night、
    // /Hard、/Primary、/Blue、/Red、/Cube、/CAD、/Headlamp。
    QString lightingScheme = QStringLiteral("/Day");
};

// 降级图像：没有 3D 运行时的阅读器用它当普通外观。
//
// 只接受**原始 RGB 像素**（每像素 3 字节，行优先），本程序不负责解码
// PNG/JPEG。为什么要这样：PDF 的图像对象要的是「解码后的采样值」，
// 塞一串 PNG 文件字节进去阅读器是解不出来的，与其写出一份结构上
// 骗人的文件，不如把格式要求写在接口上。
struct FallbackImage {
    int width = 0;
    int height = 0;
    // 原始 RGB 采样，长度必须等于 width × height × 3。
    QByteArray rgb;

    bool isValid() const {
        return width > 0 && height > 0 && rgb.size() == width * height * 3;
    }
};

// 往页面上放一个 3D 注解要带的东西。
struct ThreeDAnnotationSpec {
    // 目标页（0 基）。越界直接失败并写明原因。
    int pageIndex = 0;
    // 注解矩形（PDF user space，左下原点）。写法 QRectF(x, y, 宽, 高)，
    // 其中 y 是矩形的**下边**（PDF 的 y 向上为正），与仓库其它模块一致。
    // 宽高必须大于 0。
    QRectF rect = QRectF(0.0, 0.0, 200.0, 150.0);
    // U3D 模型字节（PRC 内容）。本程序原样搬运，不解析也不生成。
    QByteArray modelBytes;
    // 3D 视图参数。
    ThreeDViewSpec view;
    // 3D 视图框（/3DB）：模型在注解里占多大地方。
    // 空矩形表示用整个注解矩形。写进文件时会换算成 PDF 要求的
    // 「注解目标坐标系」（原点在注解矩形中心）。
    QRectF viewBox;
    // 降级图像（可选）。留空表示不写外观流——那种注解在不支持 3D 的
    // 阅读器里就是一片空白。
    FallbackImage fallback;
    // 注解是否可交互（/3DI，默认 true）。
    bool interactive = true;
    // 激活时机（/3DA 的 /A）：/PO（页面打开就激活）、/PV（可见就激活）、
    // /XA（要用户手动激活）。默认 /PO：打开就能看见。
    QString activation = QStringLiteral("/PO");
};

// 读回来的一个 3D 注解。
struct ThreeDAnnotationInfo {
    // 所在页（0 基）。
    int pageIndex = -1;
    // 注解矩形（PDF user space，y 是下边）。
    QRectF rect;
    // 3D 视图框（/3DB）：模型在注解里占多大地方，换算回 PDF user space。
    // 与 rect 相同表示「整个注解都是模型区」。
    QRectF viewBox;
    // 3D 视图参数（从 /3DV 读回来的）。
    ThreeDViewSpec view;
    // 有没有找到模型（/3DD）。
    bool hasModel = false;
    // 模型是**内嵌**在文件里，还是**外部引用**（3D 参考字典指向别处）。
    bool modelEmbedded = false;
    // 模型字节数（外链时为 0）。
    qint64 modelSize = 0;
    // 模型文件名（外链时是路径）。
    QString modelFileName;
    // 3D 流的 /Subtype（固定是 /U3D）。
    QString modelMimeType;
    // 有没有降级图像（普通外观流）。
    bool hasFallbackImage = false;
    // 注解是否可交互（/3DI）。
    bool interactive = true;
    // 激活时机（/3DA 的 /A）。
    QString activation;
};

// ============================================================================
// 多媒体
// ============================================================================

// 往页面上放一个多媒体注解要带的东西。
struct MediaAnnotationSpec {
    // 目标页（0 基）。越界直接失败并写明原因。
    int pageIndex = 0;
    // 注解矩形（PDF user space，y 是下边）。宽高必须大于 0。
    QRectF rect = QRectF(0.0, 0.0, 200.0, 150.0);
    // 媒体字节（音频/视频/图片）。内嵌时原样写进 PDF。
    QByteArray mediaBytes;
    // 文件名：用来推断 MIME 类型，也是嵌入文件的名字。
    QString fileName = QStringLiteral("media.mp4");
    // 显式 MIME 类型。空表示按扩展名推断。
    QString mimeType;
    // 内嵌（写进 PDF）还是外链（只留一个路径）。
    bool embedded = true;
    // 外链时的路径（embedded 为假时必填）。
    QString externalPath;
    // RichMedia 实例的类型（/Subtype）：/Video、/Sound、/3D、/Flash。
    // 空表示按 MIME 类型推断。
    QString instanceSubtype;
};

// 读回来的一个多媒体注解。
struct MediaAnnotationInfo {
    // 所在页（0 基）。
    int pageIndex = -1;
    // 注解矩形（PDF user space，y 是下边）。
    QRectF rect;
    // 媒体的 MIME 类型（"video/mp4" 这种写法，不带前导斜杠）。
    QString mimeType;
    // RichMedia 实例类型（/Video、/Sound、/3D、/Flash）。
    QString instanceSubtype;
    // 媒体字节数（外链时为 0）。
    qint64 mediaSize = 0;
    // 内嵌还是外链。
    bool embedded = false;
    // 文件名（内嵌时是嵌入文件的名字；外链时是路径）。
    QString fileName;
    // 外链路径（内嵌时为空）。
    QString externalPath;
};

// 读一份 PDF 里所有 3D 与多媒体注解的结果。
struct RichMediaReadResult {
    // 3D 注解（按页序、再按注解在页上的次序）。
    QVector<ThreeDAnnotationInfo> threeD;
    // 多媒体注解。
    QVector<MediaAnnotationInfo> media;
    // 中文说明：哪些地方只能按大概处理、哪些结构没读到，都写在这里。
    QStringList notes;
    // 失败原因（中文）。为空表示成功。
    QString error;

    bool ok() const { return error.isEmpty(); }
};

// ============================================================================
// 公开接口
// ============================================================================

// 往 PDF 的指定页面上放一个 3D 注解（/Subtype /3D）。
//
// 写出来的东西：/3DD 是装着 U3D 字节的 3D 流（/Subtype /U3D），
// /3DV 是 3D 视图字典（同时挂在 3D 流的 /VA 数组里），
// /3DA 是激活字典，/3DB 是 3D 视图框，有降级图像时 /AP /N 是外观流。
//
// 失败（error 非空、返回空字节）的情况：
//   * 输入不是 PDF 或内容为空；
//   * 模型字节为空（「资源字节为空 → 失败」，不写空模型）；
//   * 页码越界；
//   * 注解矩形宽高不大于 0；
//   * 降级图像给了但尺寸与像素数对不上。
//
// **重复写同一个位置**：同一页上已经有一个**矩形完全相同**的 3D 注解时，
// 直接失败并写明原因，不覆盖。两个一模一样的注解会互相遮挡，
// 界面上也没法分辨点到了哪一个，悄悄覆盖只会让人以为写成功了。
// 矩形不同的 3D 注解可以并存（PDF 规范明确允许一页放多个 3D 实例）。
QByteArray writeThreeDAnnotation(const QByteArray& pdfBytes, const ThreeDAnnotationSpec& spec,
                                  QString* error = nullptr);

// 往 PDF 的指定页面上放一个多媒体注解（/Subtype /RichMedia）。
//
// 写出来的东西：/RichMediaContent（配置字典，含 /Assets 名字树与
// /Configurations）、/RichMediaSettings（设置字典）、内嵌时用
// /Filespec + /EF 把媒体字节挂在 /Assets 名字树上。
//
// 失败（error 非空、返回空字节）的情况：
//   * 输入不是 PDF 或内容为空；
//   * 内嵌时媒体字节为空；
//   * 外链时没有给路径；
//   * 页码越界；
//   * 注解矩形宽高不大于 0。
//
// **重复写同一个位置**：与 3D 注解同一套规则——同页同矩形的多媒体注解
// 直接失败，不覆盖。
QByteArray writeMediaAnnotation(const QByteArray& pdfBytes, const MediaAnnotationSpec& spec,
                                 QString* error = nullptr);

// 读一份 PDF 里的全部 3D 与多媒体注解。
//
// 解析不了（不是 PDF、结构坏掉）时 error 非空；文件里没有这些注解不是错误，
// 只是两个列表为空。
RichMediaReadResult readRichMedia(const QByteArray& pdfBytes);

// 只读某一页上的 3D 注解（0 基）。页码越界时返回空列表并写明原因。
QVector<ThreeDAnnotationInfo> readThreeDAnnotations(const QByteArray& pdfBytes, int pageIndex,
                                                     QString* error = nullptr);

// 只读某一页上的多媒体注解（0 基）。页码越界时返回空列表并写明原因。
QVector<MediaAnnotationInfo> readMediaAnnotations(const QByteArray& pdfBytes, int pageIndex,
                                                   QString* error = nullptr);

}  // namespace aipdf::pdf
