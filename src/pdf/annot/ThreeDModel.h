#pragma once

#include <QByteArray>
#include <QColor>
#include <QImage>
#include <QRectF>
#include <QString>
#include <QStringList>
#include <QVector>
#include <QVector3D>
#include <QtEndian>
#include <QtMath>

#include <cmath>
#include <cstring>

namespace aipdf::pdf {

// ============================================================================
// 这个模块做什么、不做什么
// ============================================================================
//
// 做：
//   * 认模型文件：U3D（ECMA-363）与 PRC（ISO 14739），看文件头就能下判断。
//   * 解 U3D 的**静态网格**：顶点位置、法线、顶点色、三角形面索引。
//   * 用自写的 z-buffer 光栅化器把网格画成位图（不引第三方渲染库）。
//   * 给一个纯函数式的轨道相机（方位角 / 俯仰角 / 缩放），拖动时重渲染。
//   * 模型解不出来时退到注解的降级海报（/AP）：海报可以 2D 旋转/缩放，
//     但**它是海报不是模型**，结果里用枚举标死，界面不能说成「3D 模型」。
//
// 不做——这是取舍，不是缺陷：
//   * **不引 U3D 厂商运行时**。真正的 U3D 播放要闭源商业 SDK（要采购授权），
//     本仓库不引入。
//   * **不解 U3D 的算术压缩剖面**。ECMA-363 的 profile 里 0x04 位表示
//     「无压缩」：置了这一位的文件，所有数据都是明文小端，本程序全都能解。
//     没置这一位的文件（Acrobat 默认写出来的就是压缩剖面）要走 range 编码，
//     本程序还没实现，遇到就明确报错并退回海报——不猜、不糊。
//   * **不解动画、层级节点变换、蒙皮、材质、纹理、后处理**。解出来的是
//     CLOD 的基网格（base mesh），静态几何，看得见转得动；动起来要骨骼与
//     动画数据，那些不在这一层里。
//   * **不解析 PRC 自己的 B-rep 与装配树**。PRC 是 ISO 14739 的容器，本程序
//     只在它里面找两样东西：内嵌的 U3D 流（能解就解）与 SVG 回退表示
//     （能画就画），其余按「解不出来」处理。
//
// 关于 U3D 的量化：ECMA-363 的顶点量化只出现在渐进网格更新块
// （0xFFFFFF3C）里，那一块本程序没有解码；基网格里的顶点是 F32 明文，
// 所以本模块没有「还原量化网格」这一步——不假装做了没做的事。
//
// 「支持到哪一版规范」：ECMA-363 第 3/4 版的块结构（File Header /
// Modifier Chain / CLOD 网格生成器），且只覆盖**无压缩剖面**。

// ---------------------------------------------------------------------------
// 模型种类与表现层来源
// ---------------------------------------------------------------------------

// 模型文件种类。靠文件头认，不靠扩展名——扩展名是调用方给的，可以撒谎。
enum class ThreeDModelKind {
    Unknown = 0,
    U3D = 1,  // ECMA-363 Universal 3D
    Prc = 2,  // ISO 14739 Product Representation Compact
};

// 这次渲染出来的画面到底是什么。
//
// 为什么用枚举而不是 bool：界面上的「3D 模型」和「降级海报」是两种完全
// 不同的承诺。混成一个 bool 之后，早晚有人把海报当模型宣传出去。
// 报告、对话框、状态栏都应该拿这个枚举说话。
enum class ThreeDRenderSource {
    None = 0,   // 什么都没画出来
    Model = 1,  // 真 3D 网格：本程序自己解出来的 U3D 静态网格
    Svg = 2,    // PRC 容器里的 SVG 回退表示（2D 表现层，不是模型）
    Poster = 3, // 注解 /AP 里的降级海报（2D 表现层，不是模型）
};

// 中文说法。界面与报告都用它，不要另编一套。
inline QString threeDModelKindName(ThreeDModelKind kind) {
    switch (kind) {
        case ThreeDModelKind::U3D:
            return QStringLiteral("U3D");
        case ThreeDModelKind::Prc:
            return QStringLiteral("PRC");
        default:
            return QStringLiteral("未知");
    }
}

inline QString threeDRenderSourceName(ThreeDRenderSource source) {
    switch (source) {
        case ThreeDRenderSource::Model:
            return QStringLiteral("3D 模型");
        case ThreeDRenderSource::Svg:
            return QStringLiteral("SVG 回退表示（非模型）");
        case ThreeDRenderSource::Poster:
            return QStringLiteral("降级海报（非模型）");
        default:
            return QStringLiteral("无");
    }
}

// 只有 Model 才算「真的解出了 3D 模型」。
inline bool threeDRenderSourceIsModel(ThreeDRenderSource source) {
    return source == ThreeDRenderSource::Model;
}

// ---------------------------------------------------------------------------
// 网格
// ---------------------------------------------------------------------------

// 解出来的静态网格。
//
// 面一律是三角形：CLOD 的基网格就是三角形网格。四边形/条带那套 PolyMesh
// 编码属于 ECMA-363 早期版本，本程序没有实现，不在这里假装支持。
struct ThreeDMesh {
    // 网格名（U3D 里是生成器名，多部件模型靠它区分）。
    QString name;

    // 顶点位置（模型空间）。
    QVector<QVector3D> positions;
    // 顶点法线（模型空间）。可能为空：有些网格不带法线。
    QVector<QVector3D> normals;
    // 顶点色（RGBA）。可能为空。
    QVector<QColor> colors;

    // 三角形索引，每 3 个一组：顶点下标。
    QVector<int> faceIndices;
    // 每个角对应的法线下标，每 3 个一组；-1 表示这个角没有法线。
    QVector<int> faceNormalIndices;
    // 每个角对应的顶点色下标，每 3 个一组；-1 表示这个角没有颜色。
    QVector<int> faceColorIndices;

    // 文件头里的单位换算系数（把文件里的数值换成米）。
    double unitsScale = 1.0;

    int vertexCount() const { return positions.size(); }
    int faceCount() const { return faceIndices.size() / 3; }
    bool hasNormals() const { return !normals.isEmpty(); }
    bool hasColors() const { return !colors.isEmpty(); }

    // 包围盒。空网格返回零向量。
    QVector3D boundsMin() const {
        if (positions.isEmpty()) return QVector3D();
        QVector3D result = positions.first();
        for (const QVector3D& point : positions) {
            result.setX(qMin(result.x(), point.x()));
            result.setY(qMin(result.y(), point.y()));
            result.setZ(qMin(result.z(), point.z()));
        }
        return result;
    }

    QVector3D boundsMax() const {
        if (positions.isEmpty()) return QVector3D();
        QVector3D result = positions.first();
        for (const QVector3D& point : positions) {
            result.setX(qMax(result.x(), point.x()));
            result.setY(qMax(result.y(), point.y()));
            result.setZ(qMax(result.z(), point.z()));
        }
        return result;
    }
};

// ---------------------------------------------------------------------------
// 相机
// ---------------------------------------------------------------------------

// 轨道相机：绕模型中心转。
//
// 为什么是纯数据：拖动交互要能单测。「转了 90° 之后画面变了没有」这种话，
// 只有在旋转是一个不依赖界面的纯函数时才说得清。
struct ThreeDOrbitCamera {
    // 方位角（度）：绕竖直轴（Y）转。
    double azimuthDeg = 35.0;
    // 俯仰角（度）：绕水平轴（X）转。
    double elevationDeg = 25.0;
    // 缩放倍率：1 是原大小，越大越近。
    double zoom = 1.0;
};

// 绕 Y 轴旋转（右手系，正角把 +X 转向 -Z）。
inline QVector3D threeDRotateY(const QVector3D& point, double degrees) {
    const double radians = qDegreesToRadians(degrees);
    const double c = std::cos(radians);
    const double s = std::sin(radians);
    return QVector3D(point.x() * c + point.z() * s, point.y(), -point.x() * s + point.z() * c);
}

// 绕 X 轴旋转（右手系，正角把 +Y 转向 +Z）。
inline QVector3D threeDRotateX(const QVector3D& point, double degrees) {
    const double radians = qDegreesToRadians(degrees);
    const double c = std::cos(radians);
    const double s = std::sin(radians);
    return QVector3D(point.x(), point.y() * c - point.z() * s, point.y() * s + point.z() * c);
}

// 缩放夹取。
//
// 为什么夹：拖快了倍率会一路飞出去（0.01 倍看不清、100 倍什么都看不见），
// 而且夹住以后正反操作严格互逆，界面上的「放大/还原」才不会飘。
inline double threeDClampZoom(double zoom, double minimum = 0.1, double maximum = 10.0) {
    if (!(zoom > 0.0)) return 1.0;  // NaN 也要挡住
    return qBound(minimum, zoom, maximum);
}

// ---------------------------------------------------------------------------
// 渲染的输入与输出
// ---------------------------------------------------------------------------

// 降级海报：原始 RGB 采样（每像素 3 字节，行优先）。
//
// 为什么单独定义一份而不直接用 pdf::FallbackImage：ThreeDModel 不该反向
// 依赖写注解的那一层（那是上层），否则头文件会互相包含。
struct ThreeDPosterImage {
    int width = 0;
    int height = 0;
    QByteArray rgb;

    bool isValid() const {
        return width > 0 && height > 0 && rgb.size() == width * height * 3;
    }
};

struct ThreeDRenderOptions {
    int width = 320;
    int height = 240;
    QColor background = QColor(246, 246, 250);
    // 光照方向（模型空间，指向光源）。朗伯光照就靠它。
    QVector3D lightDirection = QVector3D(0.4, 0.7, 0.6);
    // 环境光比例：0 是全黑，1 是全亮不显立体感。
    double ambient = 0.28;
    // 视场角（度）。45 是常用的默认值。
    double fieldOfView = 45.0;
    // 网格没有顶点色时用的底色。
    QColor defaultColor = QColor(198, 202, 214);
};

struct ThreeDRenderResult {
    // 是否画出了东西。
    bool ok = false;
    // 画出来的是模型还是表现层。界面必须靠它说话。
    ThreeDRenderSource source = ThreeDRenderSource::None;
    // 渲染结果。
    QImage image;
    // 投影后的包围盒（图像像素坐标，左上原点）。海报路径给的是整张图。
    QRectF projectedBounds;
    // 失败原因（中文）。为空表示成功。
    QString error;
    // 补充说明（中文）：解到什么、没解什么。
    QStringList notes;
};

// 模型摘要：读回注解时要报的「模型类型、包围盒、网格规模」。
struct ThreeDModelInfo {
    ThreeDModelKind kind = ThreeDModelKind::Unknown;
    // 有没有解出网格。
    bool hasMesh = false;
    // 解出几个网格（多部件模型）。
    int meshCount = 0;
    // 顶点总数与三角形总数。
    int vertexCount = 0;
    int faceCount = 0;
    // 包围盒（模型空间）。
    QVector3D boundsMin;
    QVector3D boundsMax;
    // 单位换算系数（文件数值 → 米）。
    double unitsScale = 1.0;
    // 哪些没解、为什么退回（中文）。
    QStringList notes;
    // 失败原因（中文）。
    QString error;
};

// ---------------------------------------------------------------------------
// 解析（内联实现，理由见 threeDdetail 命名空间的说明）
// ---------------------------------------------------------------------------

namespace threeDdetail {

// U3D 的块类型（ECMA-363 第 3/4 版）。
inline constexpr quint32 kU3dFileHeaderBlock = 0x00443355u;
inline constexpr quint32 kU3dModifierChainBlock = 0xFFFFFF14u;
inline constexpr quint32 kU3dModelNodeBlock = 0xFFFFFF22u;
inline constexpr quint32 kU3dGroupNodeBlock = 0xFFFFFF21u;
inline constexpr quint32 kU3dClodDeclarationBlock = 0xFFFFFF31u;
inline constexpr quint32 kU3dClodBaseMeshBlock = 0xFFFFFF3Bu;
inline constexpr quint32 kU3dClodProgressiveBlock = 0xFFFFFF3Cu;

// 文件头 profile 里的标志位。
inline constexpr quint32 kU3dProfileExtensibility = 0x00000002u;
inline constexpr quint32 kU3dProfileNoCompression = 0x00000004u;
inline constexpr quint32 kU3dProfileUnitsScale = 0x00000008u;

// 小端读取器。
//
// 为什么自己写不用 QDataStream：U3D 的字符串是「u16 长度 + 字节」，
// 不是 QDataStream 的那种写法；而且读越界时必须安静地失败，不能抛异常把
// 整个读注解的过程带崩。
class ByteReader {
public:
    ByteReader(const QByteArray& bytes, int offset)
        : m_bytes(bytes), m_pos(offset) {}

    bool failed() const { return m_failed; }
    int position() const { return m_pos; }
    void seek(int position) { m_pos = position; }
    int size() const { return m_bytes.size(); }
    bool has(int count) const {
        return m_pos >= 0 && count >= 0 && m_pos + count <= m_bytes.size();
    }

    quint8 readU8() {
        if (!has(1)) {
            m_failed = true;
            return 0;
        }
        return static_cast<quint8>(m_bytes.at(m_pos++));
    }

    quint16 readU16() {
        if (!has(2)) {
            m_failed = true;
            return 0;
        }
        const quint16 value = static_cast<quint8>(m_bytes.at(m_pos))
                              | static_cast<quint16>(static_cast<quint8>(m_bytes.at(m_pos + 1))) << 8;
        m_pos += 2;
        return value;
    }

    qint16 readI16() { return static_cast<qint16>(readU16()); }

    quint32 readU32() {
        if (!has(4)) {
            m_failed = true;
            return 0;
        }
        quint32 value = 0;
        for (int index = 0; index < 4; ++index) {
            value |= static_cast<quint32>(static_cast<quint8>(m_bytes.at(m_pos + index)))
                     << (8 * index);
        }
        m_pos += 4;
        return value;
    }

    quint64 readU64() {
        const quint64 low = readU32();
        const quint64 high = readU32();
        return low | (high << 32);
    }

    float readF32() {
        const quint32 bits = readU32();
        float value = 0.0f;
#if defined(Q_BIG_ENDIAN)
        const quint32 swapped = qFromLittleEndian(bits);
        std::memcpy(&value, &swapped, sizeof(value));
#else
        std::memcpy(&value, &bits, sizeof(value));
#endif
        return value;
    }

    double readF64() {
        const quint64 bits = static_cast<quint64>(readU32())
                             | (static_cast<quint64>(readU32()) << 32);
        double value = 0.0;
#if defined(Q_BIG_ENDIAN)
        const quint64 swapped = qFromLittleEndian(bits);
        std::memcpy(&value, &swapped, sizeof(value));
#else
        std::memcpy(&value, &bits, sizeof(value));
#endif
        return value;
    }

    // U3D 字符串：u16 长度 + 字节。字符集按 UTF-8 解。
    bool readString(QString& out) {
        const quint16 length = readU16();
        if (m_failed || !has(length)) {
            m_failed = true;
            out.clear();
            return false;
        }
        out = QString::fromUtf8(m_bytes.constData() + m_pos, length);
        m_pos += length;
        return true;
    }

private:
    const QByteArray& m_bytes;
    int m_pos = 0;
    bool m_failed = false;
};

// 一个 U3D 块。
struct U3dBlock {
    quint32 type = 0;
    int dataOffset = 0;
    int dataSize = 0;
};

// 解析一段连续的 U3D 块。
//
// 块的布局是「12 字节头（类型/数据长度/元数据长度）+ 数据 + 4 字节对齐填充」。
// 填充不计入长度，所以解析时必须自己补上，否则后面每个块都会错位。
inline QVector<U3dBlock> parseU3dBlocks(const QByteArray& bytes, int offset, int end) {
    QVector<U3dBlock> blocks;
    int position = offset;
    while (position >= 0 && position + 12 <= end) {
        ByteReader reader(bytes, position);
        const quint32 type = reader.readU32();
        const quint32 dataSize = reader.readU32();
        const quint32 metaSize = reader.readU32();
        position += 12;
        if (reader.failed()) break;
        if (dataSize > static_cast<quint32>(end - position)) break;  // 截断：停下，不猜
        U3dBlock block;
        block.type = type;
        block.dataOffset = position;
        block.dataSize = static_cast<int>(dataSize);
        position += static_cast<int>(dataSize);
        position += (4 - (static_cast<int>(dataSize) % 4)) % 4;
        if (metaSize > static_cast<quint32>(end - position)) break;
        position += static_cast<int>(metaSize);
        position += (4 - (static_cast<int>(metaSize) % 4)) % 4;
        blocks.append(block);
    }
    return blocks;
}

// 修饰链块里，子块序列的起点。
//
// 链头的布局是：名字（u16 长度 + 字节）、链类型、链属性、可选的附加数据、
// 名字后面的对齐填充、修饰器个数。填充要按「名字结束的位置」算，不能按
// 「读了多少字节」算——两者在链头里有额外的 8/32 字节字段时会差开。
inline int u3dChainBodyOffset(const QByteArray& bytes, const U3dBlock& block) {
    ByteReader reader(bytes, block.dataOffset);
    const quint16 nameLength = reader.readU16();
    reader.seek(reader.position() + nameLength);
    reader.readU32();  // 链类型
    const quint32 attributes = reader.readU32();
    if (attributes & 0x1u) reader.seek(reader.position() + 16);
    if (attributes & 0x2u) reader.seek(reader.position() + 24);
    const int padding = (4 - ((2 + nameLength) % 4)) % 4;
    return reader.position() + padding + 4;  // 再跳过修饰器个数
}

// U3D 文件头。
struct U3dFileHeader {
    int major = 0;
    int minor = 0;
    quint32 profile = 0;
    quint32 declarationSize = 0;
    quint64 fileSize = 0;
    int encoding = 0;
    double unitsScale = 1.0;
    bool hasUnitsScale = false;
};

inline bool readU3dFileHeader(const QByteArray& bytes, const U3dBlock& block, U3dFileHeader& out) {
    ByteReader reader(bytes, block.dataOffset);
    out.major = reader.readU16();
    out.minor = reader.readU16();
    out.profile = reader.readU32();
    out.declarationSize = reader.readU32();
    out.fileSize = reader.readU64();
    out.encoding = static_cast<int>(reader.readU32());
    if (out.profile & kU3dProfileUnitsScale) {
        out.unitsScale = reader.readF64();
        out.hasUnitsScale = true;
    }
    return !reader.failed();
}

// 着色表的一行：决定一个面的每个角带哪些下标。
struct U3dShading {
    bool usesDiffuse = false;
    bool usesSpecular = false;
    int textureLayers = 0;
};

// CLOD 网格声明块。
struct U3dClodDeclaration {
    QString name;
    bool excludeNormals = false;
    int faceCount = 0;
    int positionCount = 0;
    int normalCount = 0;
    int diffuseCount = 0;
    int specularCount = 0;
    int texCoordCount = 0;
    QVector<U3dShading> shadings;
    double positionInverseQuant = 1.0;
};

inline bool readU3dClodDeclaration(const QByteArray& bytes, const U3dBlock& block,
                                   U3dClodDeclaration& out) {
    ByteReader reader(bytes, block.dataOffset);
    reader.readString(out.name);
    reader.readU32();  // 链下标
    out.excludeNormals = (reader.readU32() & 0x1u) != 0u;
    out.faceCount = static_cast<int>(reader.readU32());
    out.positionCount = static_cast<int>(reader.readU32());
    out.normalCount = static_cast<int>(reader.readU32());
    out.diffuseCount = static_cast<int>(reader.readU32());
    out.specularCount = static_cast<int>(reader.readU32());
    out.texCoordCount = static_cast<int>(reader.readU32());
    const int shadingCount = static_cast<int>(reader.readU32());
    for (int index = 0; index < shadingCount && !reader.failed(); ++index) {
        U3dShading shading;
        const quint32 attributes = reader.readU32();
        shading.usesDiffuse = (attributes & 0x1u) != 0u;
        shading.usesSpecular = (attributes & 0x2u) != 0u;
        shading.textureLayers = static_cast<int>(reader.readU32());
        for (int layer = 0; layer < shading.textureLayers; ++layer) {
            reader.readU32();  // 该层的纹理坐标维数
        }
        reader.readU32();  // 原始着色 id
        out.shadings.append(shading);
    }
    reader.readU32();  // 最小分辨率
    reader.readU32();  // 最终最大分辨率
    reader.readU32();  // 位置质量因子
    reader.readU32();  // 法线质量因子
    reader.readU32();  // 纹理坐标质量因子
    out.positionInverseQuant = reader.readF32();
    reader.readF32();  // 法线反量化
    reader.readF32();  // 纹理坐标反量化
    reader.readF32();  // 漫反射反量化
    reader.readF32();  // 高光反量化
    reader.readF32();  // 法线合并参数
    reader.readF32();  // 法线更新参数
    reader.readF32();  // 法线容差
    reader.readU32();  // 骨骼数
    return !reader.failed();
}

// CLOD 基网格块。
struct U3dBaseMesh {
    QString name;
    QVector<QVector3D> positions;
    QVector<QVector3D> normals;
    QVector<QColor> diffuseColors;
    QVector<int> faceIndices;
    QVector<int> faceNormalIndices;
    QVector<int> faceColorIndices;
};

// 读基网格。面数据要靠声明块里的着色表才知道每个角带哪些下标。
inline bool readU3dBaseMesh(const QByteArray& bytes, const U3dBlock& block,
                             const U3dClodDeclaration& declaration, U3dBaseMesh& out) {
    ByteReader reader(bytes, block.dataOffset);
    reader.readString(out.name);
    reader.readU32();  // 链下标
    const int faceCount = static_cast<int>(reader.readU32());
    const int positionCount = static_cast<int>(reader.readU32());
    const int normalCount = static_cast<int>(reader.readU32());
    const int diffuseCount = static_cast<int>(reader.readU32());
    const int specularCount = static_cast<int>(reader.readU32());
    const int texCoordCount = static_cast<int>(reader.readU32());
    if (reader.failed()) return false;

    // 先看一眼数组声明的规模合不合理。
    //
    // 为什么要查：坏文件里一个「顶点数」可能写成几十亿。先按字节数对一遍，
    // 既挡住越界读，也挡住 reserve() 申请一块天大的内存。
    const qint64 remaining = static_cast<qint64>(bytes.size()) - reader.position();
    const qint64 minimumBytes = static_cast<qint64>(positionCount) * 12
                                 + static_cast<qint64>(normalCount) * 12
                                 + static_cast<qint64>(diffuseCount) * 16
                                 + static_cast<qint64>(specularCount) * 16
                                 + static_cast<qint64>(texCoordCount) * 16
                                 + static_cast<qint64>(faceCount) * 16;
    if (minimumBytes > remaining) return false;

    out.positions.reserve(positionCount);
    for (int index = 0; index < positionCount && !reader.failed(); ++index) {
        const float x = reader.readF32();
        const float y = reader.readF32();
        const float z = reader.readF32();
        out.positions.append(QVector3D(x, y, z));
    }
    out.normals.reserve(normalCount);
    for (int index = 0; index < normalCount && !reader.failed(); ++index) {
        const float x = reader.readF32();
        const float y = reader.readF32();
        const float z = reader.readF32();
        out.normals.append(QVector3D(x, y, z));
    }
    out.diffuseColors.reserve(diffuseCount);
    for (int index = 0; index < diffuseCount && !reader.failed(); ++index) {
        const float r = reader.readF32();
        const float g = reader.readF32();
        const float b = reader.readF32();
        const float a = reader.readF32();
        out.diffuseColors.append(QColor::fromRgbF(qBound(0.0f, r, 1.0f), qBound(0.0f, g, 1.0f),
                                                 qBound(0.0f, b, 1.0f), qBound(0.0f, a, 1.0f)));
    }
    for (int index = 0; index < specularCount && !reader.failed(); ++index) {
        reader.readF32();
        reader.readF32();
        reader.readF32();
        reader.readF32();
    }
    for (int index = 0; index < texCoordCount && !reader.failed(); ++index) {
        reader.readF32();
        reader.readF32();
        reader.readF32();
        reader.readF32();
    }
    if (reader.failed()) return false;

    for (int face = 0; face < faceCount && !reader.failed(); ++face) {
        const quint32 shadingIndex = reader.readU32();
        U3dShading shading;
        if (static_cast<int>(shadingIndex) < declaration.shadings.size()) {
            shading = declaration.shadings.at(static_cast<int>(shadingIndex));
        }
        for (int corner = 0; corner < 3 && !reader.failed(); ++corner) {
            out.faceIndices.append(static_cast<int>(reader.readU32()));
            if (!declaration.excludeNormals) {
                out.faceNormalIndices.append(static_cast<int>(reader.readU32()));
            } else {
                out.faceNormalIndices.append(-1);
            }
            if (shading.usesDiffuse) {
                out.faceColorIndices.append(static_cast<int>(reader.readU32()));
            } else {
                out.faceColorIndices.append(-1);
            }
            if (shading.usesSpecular) reader.readU32();
            for (int layer = 0; layer < shading.textureLayers; ++layer) reader.readU32();
        }
    }
    return !reader.failed();
}

// 把文件里所有 CLOD 块收齐：声明块与基网格块靠名字配对
// （声明块通常在修饰链里，基网格块通常在顶层，两边不是嵌套关系）。
inline void collectClodBlocks(const QByteArray& bytes, const QVector<U3dBlock>& blocks,
                               QVector<U3dBlock>& declarations, QVector<U3dBlock>& baseMeshes,
                               QVector<U3dBlock>& progressive, int depth = 0) {
    if (depth > 8) return;  // 坏文件里的嵌套可能很深，不设限会把自己栈爆掉
    for (const U3dBlock& block : blocks) {
        switch (block.type) {
            case kU3dClodDeclarationBlock:
                declarations.append(block);
                break;
            case kU3dClodBaseMeshBlock:
                baseMeshes.append(block);
                break;
            case kU3dClodProgressiveBlock:
                progressive.append(block);
                break;
            default:
                break;
        }
        if (block.type == kU3dModifierChainBlock) {
            const int body = u3dChainBodyOffset(bytes, block);
            if (body <= 0 || body > block.dataOffset + block.dataSize) continue;
            collectClodBlocks(bytes,
                              parseU3dBlocks(bytes, body, block.dataOffset + block.dataSize),
                              declarations, baseMeshes, progressive, depth + 1);
        }
    }
}

// 读一个块的第一个字符串（块的第一个字段就是名字）。
inline QString u3dBlockName(const QByteArray& bytes, const U3dBlock& block) {
    ByteReader reader(bytes, block.dataOffset);
    QString name;
    reader.readString(name);
    return name;
}

}  // namespace threeDdetail

// ---------------------------------------------------------------------------
// 公开接口（解析部分）
// ---------------------------------------------------------------------------

// 看文件头认模型种类。
//
// U3D 的文件头块类型是 0x00443355，小端读出来就是 ASCII 的 "U3D\0"；
// PRC 的注册签名是 ASCII 的 "PRC" 三个字节。
inline ThreeDModelKind threeDDetectModelKind(const QByteArray& bytes) {
    if (bytes.size() >= 4) {
        const quint32 magic = static_cast<quint32>(static_cast<quint8>(bytes.at(0)))
                              | (static_cast<quint32>(static_cast<quint8>(bytes.at(1))) << 8)
                              | (static_cast<quint32>(static_cast<quint8>(bytes.at(2))) << 16)
                              | (static_cast<quint32>(static_cast<quint8>(bytes.at(3))) << 24);
        if (magic == threeDdetail::kU3dFileHeaderBlock) return ThreeDModelKind::U3D;
    }
    if (bytes.size() >= 3 && bytes.at(0) == 'P' && bytes.at(1) == 'R' && bytes.at(2) == 'C') {
        return ThreeDModelKind::Prc;
    }
    return ThreeDModelKind::Unknown;
}

// 解 U3D 的静态网格。
//
// 解不出来时 error 里写中文原因，notes 里写解到什么、没解什么。
// 这段是内联实现，因为读 3D 注解时也要用它报网格规模——注解读写那一层
// 必须在没有渲染器的情况下也能把模型信息报出来。
inline bool threeDDecodeU3dMesh(const QByteArray& bytes, QVector<ThreeDMesh>& meshes,
                                QString* error, QStringList* notes = nullptr) {
    meshes.clear();
    QString localError;
    QStringList localNotes;
    const auto fail = [&localError, &localNotes, error, notes](const QString& message) {
        localError = message;
        if (error != nullptr) *error = message;
        if (notes != nullptr) *notes = localNotes;
        return false;
    };

    if (bytes.size() < 16) {
        return fail(QStringLiteral("U3D 数据太短（%1 字节），连文件头块都读不出来").arg(bytes.size()));
    }
    const QVector<threeDdetail::U3dBlock> topBlocks =
        threeDdetail::parseU3dBlocks(bytes, 0, bytes.size());
    const threeDdetail::U3dBlock* header = nullptr;
    for (const threeDdetail::U3dBlock& block : topBlocks) {
        if (block.type == threeDdetail::kU3dFileHeaderBlock) {
            header = &block;
            break;
        }
    }
    if (header == nullptr) {
        return fail(QStringLiteral("不是 U3D 文件：找不到文件头块（0x00443355）"));
    }
    threeDdetail::U3dFileHeader fileHeader;
    if (!threeDdetail::readU3dFileHeader(bytes, *header, fileHeader)) {
        return fail(QStringLiteral("U3D 文件头块读不出来，内容可能已损坏"));
    }
    if ((fileHeader.profile & threeDdetail::kU3dProfileNoCompression) == 0u) {
        return fail(QStringLiteral("这个 U3D 用的是算术压缩剖面（profile=0x%1），"
                                   "本程序只会解无压缩剖面（profile 的 0x04 位），所以取不出网格")
                        .arg(fileHeader.profile, 8, 16, QLatin1Char('0')));
    }
    localNotes.append(QStringLiteral("U3D 剖面 0x%1，单位换算系数 %2")
                          .arg(fileHeader.profile, 8, 16, QLatin1Char('0'))
                          .arg(fileHeader.hasUnitsScale ? fileHeader.unitsScale : 1.0));

    QVector<threeDdetail::U3dBlock> declarations;
    QVector<threeDdetail::U3dBlock> baseMeshes;
    QVector<threeDdetail::U3dBlock> progressive;
    threeDdetail::collectClodBlocks(bytes, topBlocks, declarations, baseMeshes, progressive);
    if (!progressive.isEmpty()) {
        localNotes.append(QStringLiteral("文件里有 %1 个渐进网格更新块（0xFFFFFF3C），本程序没有解码，"
                                          "只用了基网格")
                              .arg(progressive.size()));
    }
    if (baseMeshes.isEmpty()) {
        return fail(QStringLiteral("U3D 里没有找到基网格块（0xFFFFFF3B）"));
    }

    for (const threeDdetail::U3dBlock& baseBlock : baseMeshes) {
        const QString meshName = threeDdetail::u3dBlockName(bytes, baseBlock);
        const threeDdetail::U3dBlock* declarationBlock = nullptr;
        for (const threeDdetail::U3dBlock& candidate : declarations) {
            if (threeDdetail::u3dBlockName(bytes, candidate) == meshName) {
                declarationBlock = &candidate;
                break;
            }
        }
        if (declarationBlock == nullptr) {
            localNotes.append(QStringLiteral("网格「%1」找不到配对的 CLOD 声明块，跳过了").arg(meshName));
            continue;
        }
        threeDdetail::U3dClodDeclaration declaration;
        if (!threeDdetail::readU3dClodDeclaration(bytes, *declarationBlock, declaration)) {
            localNotes.append(QStringLiteral("网格「%1」的 CLOD 声明块读不出来，跳过了").arg(meshName));
            continue;
        }
        threeDdetail::U3dBaseMesh base;
        if (!threeDdetail::readU3dBaseMesh(bytes, baseBlock, declaration, base)) {
            localNotes.append(QStringLiteral("网格「%1」的基网格块读不出来，跳过了").arg(meshName));
            continue;
        }
        ThreeDMesh mesh;
        mesh.name = meshName.isEmpty() ? QStringLiteral("未命名网格") : meshName;
        mesh.positions = base.positions;
        mesh.normals = base.normals;
        mesh.colors = base.diffuseColors;
        mesh.faceIndices = base.faceIndices;
        mesh.faceNormalIndices = base.faceNormalIndices;
        mesh.faceColorIndices = base.faceColorIndices;
        mesh.unitsScale = fileHeader.hasUnitsScale ? fileHeader.unitsScale : 1.0;
        meshes.append(mesh);
    }

    if (meshes.isEmpty()) {
        return fail(QStringLiteral("U3D 里没有解出可用的网格"));
    }
    if (error != nullptr) error->clear();
    if (notes != nullptr) *notes = localNotes;
    return true;
}

// 在 PRC 容器里找内嵌的 U3D 流。
//
// PRC 容器里常见两种东西：一份 U3D，和一份 SVG 回退表示。U3D 的文件头块
// 类型是固定值，所以在容器里扫这个 4 字节签名就能定位到它的起点。
inline int threeDFindEmbeddedU3dOffset(const QByteArray& bytes) {
    if (bytes.size() < 4) return -1;
    for (int offset = 0; offset + 4 <= bytes.size(); ++offset) {
        if (static_cast<quint8>(bytes.at(offset)) == 0x55
            && static_cast<quint8>(bytes.at(offset + 1)) == 0x33
            && static_cast<quint8>(bytes.at(offset + 2)) == 0x44
            && static_cast<quint8>(bytes.at(offset + 3)) == 0x00) {
            return offset;
        }
    }
    return -1;
}

// 在 PRC 容器里找内嵌的 SVG。
//
// 找法是从 "<svg" 扫到 "</svg>"（大小写都认）。这不是完整的 XML 解析，
// 但 PRC 里的 SVG 是整段文本，扫一遍足够定位；解析不了就当没有。
inline QByteArray threeDFindEmbeddedSvg(const QByteArray& bytes) {
    const QByteArray lowered = bytes.toLower();
    const int start = lowered.indexOf("<svg");
    if (start < 0) return {};
    const int end = lowered.indexOf("</svg>", start);
    if (end < 0) return {};
    return bytes.mid(start, end + 6 - start);
}

// 解析模型并给出摘要：类型、包围盒、网格规模。
inline bool threeDInspectModel(const QByteArray& bytes, ThreeDModelInfo& info) {
    info = ThreeDModelInfo();
    info.kind = threeDDetectModelKind(bytes);
    if (info.kind == ThreeDModelKind::Unknown) {
        info.error = QStringLiteral("模型文件头既不像 U3D（应以 \"U3D\\0\" 开头）"
                                    "也不像 PRC（应以 \"PRC\" 开头）");
        return false;
    }
    if (info.kind == ThreeDModelKind::Prc) {
        info.notes.append(QStringLiteral("PRC 是 ISO 14739 的容器，本程序不解析它的 B-rep 与装配树，"
                                         "只在里面找内嵌的 U3D 与 SVG"));
        const int offset = threeDFindEmbeddedU3dOffset(bytes);
        if (offset < 0) {
            info.error = QStringLiteral("PRC 容器里没有找到内嵌的 U3D 流");
            return false;
        }
        QVector<ThreeDMesh> meshes;
        QString error;
        QStringList notes;
        if (!threeDDecodeU3dMesh(bytes.mid(offset), meshes, &error, &notes)) {
            info.notes.append(error);
            info.error = error;
            return false;
        }
        info.notes.append(notes);
        info.notes.append(QStringLiteral("网格来自 PRC 容器内嵌的 U3D"));
        if (meshes.isEmpty()) {
            info.error = QStringLiteral("PRC 内嵌的 U3D 里没有解出可用的网格");
            return false;
        }
        info.hasMesh = true;
        info.meshCount = meshes.size();
        for (const ThreeDMesh& mesh : meshes) {
            info.vertexCount += mesh.vertexCount();
            info.faceCount += mesh.faceCount();
        }
        const ThreeDMesh& first = meshes.first();
        info.boundsMin = first.boundsMin();
        info.boundsMax = first.boundsMax();
        info.unitsScale = first.unitsScale;
        return true;
    }

    QVector<ThreeDMesh> meshes;
    QString error;
    QStringList notes;
    if (!threeDDecodeU3dMesh(bytes, meshes, &error, &notes)) {
        info.notes = notes;
        info.error = error;
        return false;
    }
    info.notes = notes;
    if (meshes.isEmpty()) {
        info.error = QStringLiteral("U3D 里没有解出可用的网格");
        return false;
    }
    info.hasMesh = true;
    info.meshCount = meshes.size();
    for (const ThreeDMesh& mesh : meshes) {
        info.vertexCount += mesh.vertexCount();
        info.faceCount += mesh.faceCount();
    }
    const ThreeDMesh& first = meshes.first();
    info.boundsMin = first.boundsMin();
    info.boundsMax = first.boundsMax();
    info.unitsScale = first.unitsScale;
    return true;
}

// ---------------------------------------------------------------------------
// 公开接口（渲染部分，实现在 ThreeDModel.cpp）
// ---------------------------------------------------------------------------

// 渲染入口：真模型 → PRC 里的 SVG → 降级海报。
//
// 顺序是有讲究的：能解出网格就用网格（那是用户要的「3D 模型」）；
// 解不出但 PRC 里有 SVG 就画 SVG；再不行才退到海报。
// 走哪条路由 result.source 标死，界面据此说明白。
ThreeDRenderResult threeDRenderModel(const QByteArray& modelBytes, const ThreeDPosterImage& poster,
                                      const ThreeDOrbitCamera& camera,
                                      const ThreeDRenderOptions& options);

// 只渲染网格（多部件模型会一起画）。
ThreeDRenderResult threeDRenderMesh(const QVector<ThreeDMesh>& meshes, const ThreeDOrbitCamera& camera,
                                     const ThreeDRenderOptions& options);

// 只渲染海报：按方位角做平面旋转、按俯仰角做竖向压扁、按缩放放大缩小。
//
// 这是 2D 变换，**不是 3D 模型旋转**——结果里的 source 一定是 Poster。
ThreeDRenderResult threeDRenderPoster(const ThreeDPosterImage& poster, const ThreeDOrbitCamera& camera,
                                       const ThreeDRenderOptions& options);

}  // namespace aipdf::pdf
