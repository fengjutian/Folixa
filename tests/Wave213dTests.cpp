// 3D 注解的嵌入、解码与表现层渲染。
//
// 这一组用例钉死四件事：
//   1. 模型能真的嵌进 PDF（/3DD 流），并且能读回来；
//   2. U3D 的静态网格能被解出来（顶点、法线、颜色、面）；
//   3. 解出来的网格能渲染成位图，并且转得动；
//   4. 模型解不出来时走海报/SVG 表现层，而且**接口能分辨走的是哪条**。
//
// 夹具是自己按 ECMA-363 的块结构写出来的最小 U3D 文件：
// 文件头块 + 节点修饰链 + 资源修饰链 + CLOD 声明块 + CLOD 基网格块。
// 为什么自己写而不是找个现成的文件：现成的 U3D 大多是算术压缩剖面，
// 本程序还没实现 range 编码解码，拿那种文件来测只会测出一个「解不开」。
#include "pdf/annot/RichMedia3d.h"
#include "pdf/annot/ThreeDModel.h"

#include <QColor>
#include <QTest>
#include <QVector3D>

#include <fpdfview.h>

#include <cstring>

using aipdf::pdf::readThreeDAnnotations;
using aipdf::pdf::ThreeDAnnotationSpec;
using aipdf::pdf::ThreeDAnnotationInfo;
using aipdf::pdf::ThreeDModelKind;
using aipdf::pdf::ThreeDModelInfo;
using aipdf::pdf::ThreeDMesh;
using aipdf::pdf::ThreeDOrbitCamera;
using aipdf::pdf::ThreeDPosterImage;
using aipdf::pdf::ThreeDRenderOptions;
using aipdf::pdf::ThreeDRenderResult;
using aipdf::pdf::ThreeDRenderSource;
using aipdf::pdf::threeDClampZoom;
using aipdf::pdf::threeDDecodeU3dMesh;
using aipdf::pdf::threeDDetectModelKind;
using aipdf::pdf::threeDInspectModel;
using aipdf::pdf::threeDRenderMesh;
using aipdf::pdf::threeDRenderModel;
using aipdf::pdf::threeDRenderSourceIsModel;
using aipdf::pdf::threeDRenderSourceName;
using aipdf::pdf::threeDRotateX;
using aipdf::pdf::threeDRotateY;
using aipdf::pdf::writeThreeDAnnotation;

namespace {

// ---------------------------------------------------------------------------
// 写字节的小工具（U3D 是小端）
// ---------------------------------------------------------------------------
class ByteWriter {
public:
    void u8(quint8 value) { m_data.append(static_cast<char>(value)); }

    void u16(quint16 value) {
        m_data.append(static_cast<char>(value & 0xFF));
        m_data.append(static_cast<char>((value >> 8) & 0xFF));
    }

    void u32(quint32 value) {
        u16(static_cast<quint16>(value & 0xFFFF));
        u16(static_cast<quint16>((value >> 16) & 0xFFFF));
    }

    void u64(quint64 value) {
        u32(static_cast<quint32>(value & 0xFFFFFFFFu));
        u32(static_cast<quint32>((value >> 32) & 0xFFFFFFFFu));
    }

    void f32(float value) {
        quint32 bits = 0;
        std::memcpy(&bits, &value, sizeof(bits));
        u32(bits);
    }

    void f64(double value) {
        quint64 bits = 0;
        std::memcpy(&bits, &value, sizeof(bits));
        u64(bits);
    }

    // U3D 字符串：u16 长度 + UTF-8 字节。
    void string(const QString& text) {
        const QByteArray raw = text.toUtf8();
        u16(static_cast<quint16>(raw.size()));
        m_data.append(raw);
    }

    void raw(const QByteArray& bytes) { m_data.append(bytes); }

    // 补零到 4 字节边界。
    void pad4() {
        while (m_data.size() % 4 != 0) m_data.append('\0');
    }

    int size() const { return m_data.size(); }
    const QByteArray& data() const { return m_data; }

private:
    QByteArray m_data;
};

// 一个 U3D 块：12 字节头（类型/数据长度/元数据长度）+ 数据 + 4 字节对齐填充。
QByteArray u3dBlock(quint32 type, const QByteArray& payload) {
    ByteWriter writer;
    writer.u32(type);
    writer.u32(static_cast<quint32>(payload.size()));
    writer.u32(0);  // 元数据长度
    writer.raw(payload);
    writer.pad4();
    return writer.data();
}

// ---------------------------------------------------------------------------
// 夹具网格
// ---------------------------------------------------------------------------
struct MeshFixture {
    QString name;
    QVector<QVector3D> positions;
    QVector<QVector3D> normals;
    QVector<QColor> colors;
    QVector<QVector<int>> faces;  // 每个面三个顶点下标
};

// 造一个长方体：8 个顶点、12 个三角形。
//
// withNormalsAndColors 为假时只给顶点位置——那对应 U3D 里「不带法线」的
// 网格（声明块的 0x1 标志），解码时逐角法线下标必须标成 -1。
//
// 为什么面法线而不是平滑法线：面法线能同时验「法线有没有解出来」和
// 「光照是不是真的用上了法线」——平滑法线全一样的话，光照差异看不出来。
MeshFixture makeBox(const QVector3D& low, const QVector3D& high, const QColor& faceColor,
                     bool withNormalsAndColors = true) {
    MeshFixture mesh;
    mesh.name = QStringLiteral("Box");
    mesh.positions = {
        QVector3D(low.x(), low.y(), low.z()),   // 0
        QVector3D(high.x(), low.y(), low.z()),  // 1
        QVector3D(high.x(), high.y(), low.z()),  // 2
        QVector3D(low.x(), high.y(), low.z()),  // 3
        QVector3D(low.x(), low.y(), high.z()),  // 4
        QVector3D(high.x(), low.y(), high.z()),  // 5
        QVector3D(high.x(), high.y(), high.z()),  // 6
        QVector3D(low.x(), high.y(), high.z()),  // 7
    };
    const QVector<QVector<int>> faces = {
        {4, 5, 6}, {4, 6, 7},   // +Z
        {1, 0, 3}, {1, 3, 2},   // -Z
        {0, 4, 7}, {0, 7, 3},   // -X
        {5, 1, 2}, {5, 2, 6},   // +X
        {3, 7, 6}, {3, 6, 2},   // +Y
        {0, 1, 5}, {0, 5, 4},   // -Y
    };
    for (const QVector<int>& face : faces) {
        mesh.faces.append(face);
        if (!withNormalsAndColors) continue;
        const QVector3D& a = mesh.positions.at(face.at(0));
        const QVector3D& b = mesh.positions.at(face.at(1));
        const QVector3D& c = mesh.positions.at(face.at(2));
        QVector3D normal = QVector3D::crossProduct(b - a, c - a);
        normal.normalize();
        // 每个角都带一份法线与颜色：这样逐角下标也一并被验到。
        for (int corner = 0; corner < 3; ++corner) {
            mesh.normals.append(normal);
            mesh.colors.append(faceColor);
        }
    }
    return mesh;
}

// 造一个细分过的立方体：每个面 3×3 的网格 → 54 个顶点、48 个三角形。
//
// 为什么要有「几十个三角形」的网格：只有 12 个三角形时，光栅化器的边函数
// 与深度缓冲很容易在小规模上侥幸正确；三角形多几个，错位才会露出来。
MeshFixture makeDetailedCube() {
    MeshFixture mesh;
    mesh.name = QStringLiteral("DetailedCube");
    struct FaceDefinition {
        QVector3D origin;
        QVector3D uAxis;
        QVector3D vAxis;
        QColor color;
    };
    const QVector<FaceDefinition> faces = {
        {QVector3D(1, 0, 0), QVector3D(0, 0, -1), QVector3D(0, 1, 0), QColor(214, 96, 77)},
        {QVector3D(0, 0, 1), QVector3D(0, 0, 1), QVector3D(0, 1, 0), QColor(77, 154, 214)},
        {QVector3D(0, 1, 0), QVector3D(1, 0, 0), QVector3D(0, 0, -1), QColor(122, 201, 128)},
        {QVector3D(0, 0, 1), QVector3D(1, 0, 0), QVector3D(0, 0, 1), QColor(226, 184, 92)},
        {QVector3D(0, 0, 1), QVector3D(1, 0, 0), QVector3D(0, 1, 0), QColor(168, 130, 214)},
        {QVector3D(1, 0, 0), QVector3D(-1, 0, 0), QVector3D(0, 1, 0), QColor(94, 194, 197)},
    };
    for (const FaceDefinition& face : faces) {
        QVector3D normal = QVector3D::crossProduct(face.uAxis, face.vAxis);
        normal.normalize();
        const int base = mesh.positions.size();
        for (int row = 0; row <= 2; ++row) {
            for (int column = 0; column <= 2; ++column) {
                const double u = column / 2.0;
                const double v = row / 2.0;
                mesh.positions.append(face.origin + face.uAxis * u + face.vAxis * v);
                mesh.normals.append(normal);
                // 颜色按格点微调，好让「逐角颜色」在图上真的看得出差别。
                const int shade = 40 * (row + column);
                mesh.colors.append(QColor::fromRgbF(qBound(0.0, face.color.redF() - shade / 255.0, 1.0),
                                                    qBound(0.0, face.color.greenF() - shade / 255.0, 1.0),
                                                    qBound(0.0, face.color.blueF() - shade / 255.0, 1.0)));
            }
        }
        for (int row = 0; row < 2; ++row) {
            for (int column = 0; column < 2; ++column) {
                const int topLeft = base + row * 3 + column;
                const int topRight = topLeft + 1;
                const int bottomLeft = topLeft + 3;
                const int bottomRight = bottomLeft + 1;
                mesh.faces.append(QVector<int>{topLeft, topRight, bottomRight});
                mesh.faces.append(QVector<int>{topLeft, bottomRight, bottomLeft});
            }
        }
    }
    return mesh;
}

// ---------------------------------------------------------------------------
// 造 U3D 文件
// ---------------------------------------------------------------------------

// CLOD 声明块的内容。
QByteArray clodDeclarationPayload(const MeshFixture& mesh) {
    const bool excludeNormals = mesh.normals.isEmpty();
    const bool usesDiffuse = !mesh.colors.isEmpty();
    ByteWriter writer;
    writer.string(mesh.name);
    writer.u32(0);  // 链下标
    writer.u32(excludeNormals ? 1u : 0u);  // 网格属性：0x1 = 不带法线
    writer.u32(static_cast<quint32>(mesh.faces.size()));
    writer.u32(static_cast<quint32>(mesh.positions.size()));
    writer.u32(static_cast<quint32>(mesh.normals.size()));
    writer.u32(static_cast<quint32>(mesh.colors.size()));
    writer.u32(0);  // 高光颜色数
    writer.u32(0);  // 纹理坐标数
    writer.u32(1);  // 着色表行数
    writer.u32(usesDiffuse ? 1u : 0u);  // 着色属性：0x1 = 用逐顶点漫反射色
    writer.u32(0);  // 纹理层数
    writer.u32(0);  // 原始着色 id
    writer.u32(static_cast<quint32>(mesh.positions.size()));  // 最小分辨率
    writer.u32(static_cast<quint32>(mesh.positions.size()));  // 最终最大分辨率
    writer.u32(0);  // 位置质量因子
    writer.u32(0);  // 法线质量因子
    writer.u32(0);  // 纹理坐标质量因子
    writer.f32(1.0f);  // 位置反量化
    writer.f32(1.0f);  // 法线反量化
    writer.f32(1.0f);  // 纹理坐标反量化
    writer.f32(1.0f);  // 漫反射反量化
    writer.f32(1.0f);  // 高光反量化
    writer.f32(0.0f);  // 法线合并参数
    writer.f32(0.0f);  // 法线更新参数
    writer.f32(0.0f);  // 法线容差
    writer.u32(0);  // 骨骼数
    return writer.data();
}

// CLOD 基网格块的内容。
QByteArray clodBaseMeshPayload(const MeshFixture& mesh) {
    const bool excludeNormals = mesh.normals.isEmpty();
    const bool usesDiffuse = !mesh.colors.isEmpty();
    ByteWriter writer;
    writer.string(mesh.name);
    writer.u32(0);  // 链下标
    writer.u32(static_cast<quint32>(mesh.faces.size()));
    writer.u32(static_cast<quint32>(mesh.positions.size()));
    writer.u32(static_cast<quint32>(mesh.normals.size()));
    writer.u32(static_cast<quint32>(mesh.colors.size()));
    writer.u32(0);  // 高光颜色数
    writer.u32(0);  // 纹理坐标数
    for (const QVector3D& position : mesh.positions) {
        writer.f32(position.x());
        writer.f32(position.y());
        writer.f32(position.z());
    }
    for (const QVector3D& normal : mesh.normals) {
        writer.f32(normal.x());
        writer.f32(normal.y());
        writer.f32(normal.z());
    }
    for (const QColor& color : mesh.colors) {
        writer.f32(color.redF());
        writer.f32(color.greenF());
        writer.f32(color.blueF());
        writer.f32(1.0f);
    }
    for (const QVector<int>& face : mesh.faces) {
        writer.u32(0);  // 着色 id
        for (int corner = 0; corner < 3; ++corner) {
            const int vertexIndex = face.at(corner);
            writer.u32(static_cast<quint32>(vertexIndex));
            if (!excludeNormals) {
                writer.u32(static_cast<quint32>(vertexIndex));
            }
            if (usesDiffuse) {
                writer.u32(static_cast<quint32>(vertexIndex));
            }
        }
    }
    return writer.data();
}

// 修饰链块的内容：名字 + 链类型 + 属性 + 修饰器个数 + 子块。
//
// 名字后面有一段对齐填充，这是规范里最容易漏掉的一处：漏了之后链里
// 第一个子块的块类型就会错位，后面全乱。
QByteArray modifierChainPayload(const QString& name, quint32 chainType, const QByteArray& child) {
    ByteWriter writer;
    writer.string(name);
    writer.u32(chainType);
    writer.u32(0);  // 链属性
    const int padding = (4 - ((2 + name.toUtf8().size()) % 4)) % 4;
    for (int index = 0; index < padding; ++index) writer.u8(0);
    writer.u32(1);  // 修饰器个数
    writer.raw(child);
    return writer.data();
}

// 造一个 U3D 文件。
//
// 布局照真实 U3D 来：声明块在资源修饰链里，基网格块在顶层，两边靠名字配对。
QByteArray buildU3d(const QVector<MeshFixture>& meshes, bool compressedProfile = false) {
    const QString nodeName = QStringLiteral("MeshNode");
    const QString resourceName = QStringLiteral("MeshResource");

    ByteWriter modelNode;
    modelNode.string(nodeName);
    modelNode.u32(0);  // 父节点个数
    modelNode.string(resourceName);
    modelNode.u32(3);  // 可见性：前后都可见
    const QByteArray nodeChain = u3dBlock(0xFFFFFF14u,
                                          modifierChainPayload(nodeName, 0u, u3dBlock(0xFFFFFF22u, modelNode.data())));

    QByteArray resourceChainPayload;
    for (const MeshFixture& mesh : meshes) {
        resourceChainPayload.append(u3dBlock(0xFFFFFF31u, clodDeclarationPayload(mesh)));
    }
    const QByteArray resourceChain = u3dBlock(0xFFFFFF14u,
                                              modifierChainPayload(resourceName, 1u, resourceChainPayload));

    QByteArray baseMeshes;
    for (const MeshFixture& mesh : meshes) {
        baseMeshes.append(u3dBlock(0xFFFFFF3Bu, clodBaseMeshPayload(mesh)));
    }

    const QByteArray body = nodeChain + resourceChain + baseMeshes;
    const quint32 declarationSize = static_cast<quint32>(nodeChain.size() + resourceChain.size());
    const quint64 fileSize = static_cast<quint64>(44 + body.size());

    ByteWriter header;
    header.u16(0);  // 主版本
    header.u16(0);  // 次版本
    // profile：0x04 = 无压缩、0x08 = 定义了单位。
    header.u32(compressedProfile ? 0x00000008u : 0x0000000Cu);
    header.u32(declarationSize);
    header.u64(fileSize);
    header.u32(106);  // 字符集：UTF-8
    header.f64(1.0);  // 单位换算系数
    const QByteArray headerBlock = u3dBlock(0x00443355u, header.data());
    return headerBlock + body;
}

// 造一个 PRC 容器。
//
// 本程序不解析 PRC 自己的结构，只在里面扫内嵌的 U3D 与 SVG，
// 所以这个夹具只要「签名对、里面塞得下东西」就够。
QByteArray buildPrc(const QByteArray& embeddedU3d, const QByteArray& embeddedSvg) {
    QByteArray prc("PRC");
    prc.append(QByteArray(12, '\x00'));
    prc.append(QByteArray("PRC-HEADER-FILLER", 17));
    prc.append(embeddedU3d);
    prc.append(QByteArray(8, '\x00'));
    if (!embeddedSvg.isEmpty()) {
        prc.append(embeddedSvg);
    }
    prc.append(QByteArray(16, '\x00'));
    return prc;
}

// 一张最小的 SVG（够 QSvgRenderer 认出来）。
QByteArray simpleSvg() {
    return QByteArray(
        "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"40\" height=\"20\">"
        "<rect x=\"0\" y=\"0\" width=\"40\" height=\"20\" fill=\"#2f6fbf\"/>"
        "<rect x=\"4\" y=\"4\" width=\"12\" height=\"12\" fill=\"#f0c040\"/>"
        "</svg>");
}

// 一张 4×2 的降级海报：左边红、右边蓝，旋转 90° 后左右会互换。
ThreeDPosterImage makePoster() {
    ThreeDPosterImage poster;
    poster.width = 4;
    poster.height = 2;
    poster.rgb = QByteArray();
    for (int row = 0; row < 2; ++row) {
        for (int column = 0; column < 4; ++column) {
            if (column < 2) {
                poster.rgb.append(static_cast<char>(220));
                poster.rgb.append(static_cast<char>(30));
                poster.rgb.append(static_cast<char>(30));
            } else {
                poster.rgb.append(static_cast<char>(30));
                poster.rgb.append(static_cast<char>(30));
                poster.rgb.append(static_cast<char>(220));
            }
        }
    }
    return poster;
}

// ---------------------------------------------------------------------------
// 一页的普通 PDF（用来放 3D 注解）
// ---------------------------------------------------------------------------
QByteArray onePagePdf() {
    const QByteArray content = "BT /F1 12 Tf 40 250 Td (Page one) Tj ET";
    QByteArray out("%PDF-1.7\n%\xE2\xE3\xCF\xD3\n");
    QHash<int, int> offsets;
    const auto appendObject = [&out, &offsets](int id, const QByteArray& body) {
        offsets.insert(id, out.size());
        out += QStringLiteral("%1 0 obj\n").arg(id).toLatin1();
        out += body;
        out += QByteArray("\nendobj\n");
    };
    appendObject(1, "<< /Type /Catalog /Pages 2 0 R >>");
    appendObject(2, "<< /Type /Pages /Kids [3 0 R] /Count 1 >>");
    appendObject(3, "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 400 300] /Resources << >> "
                    "/Contents 4 0 R >>");
    appendObject(4, "<< /Length " + QByteArray::number(content.size()) + " >>\nstream\n" + content
                    + "\nendstream");
    const int xrefOffset = out.size();
    out += QStringLiteral("xref\n0 5\n0000000000 65535 f \n").toLatin1();
    for (int id = 1; id <= 4; ++id) {
        out += QStringLiteral("%1 00000 n \n")
                   .arg(offsets.value(id, 0), 10, 10, QLatin1Char('0'))
                   .toLatin1();
    }
    out += QStringLiteral("trailer\n<< /Size 5 /Root 1 0 R >>\nstartxref\n%1\n%%EOF\n")
               .arg(xrefOffset)
               .toLatin1();
    return out;
}

// ---------------------------------------------------------------------------
// 用 PDFium 验「写出去的文件还能不能被打开」
// ---------------------------------------------------------------------------

// PDFium 的库初始化（只做一次）。
void initPdfium() {
    FPDF_LIBRARY_CONFIG config = {};
    config.version = 2;
    FPDF_InitLibraryWithConfig(&config);
}

// 用 PDFium 打开一段字节，返回页数；打不开返回 -1。
//
// 为什么用 PDFium 验：光比对字节没有意义——文件能不能被真的打开、
// 页数对不对，只有解析器说了算。
int pageCountOf(const QByteArray& bytes) {
    FPDF_DOCUMENT document =
        FPDF_LoadMemDocument(bytes.constData(), static_cast<int>(bytes.size()), nullptr);
    if (document == nullptr) return -1;
    const int count = FPDF_GetPageCount(document);
    FPDF_CloseDocument(document);
    return count;
}

// ---------------------------------------------------------------------------
// 图像比较
// ---------------------------------------------------------------------------
// 两张图里不一样的像素个数。
int differentPixelCount(const QImage& left, const QImage& right) {
    if (left.size() != right.size()) return -1;
    int count = 0;
    for (int y = 0; y < left.height(); ++y) {
        for (int x = 0; x < left.width(); ++x) {
            if (left.pixel(x, y) != right.pixel(x, y)) ++count;
        }
    }
    return count;
}

// 不是背景色的像素个数。
int nonBackgroundPixelCount(const QImage& image, const QColor& background) {
    int count = 0;
    const QRgb backgroundRgb = background.rgb();
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            if (image.pixel(x, y) != backgroundRgb) ++count;
        }
    }
    return count;
}

}  // namespace

class Wave213dTests : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();

    // ---- 文件头识别 ----

    void theModelKindComesFromTheFileHeader() {
        QCOMPARE(threeDDetectModelKind(m_cubeU3d), ThreeDModelKind::U3D);
        QCOMPARE(threeDDetectModelKind(buildPrc(QByteArray(), QByteArray())), ThreeDModelKind::Prc);
        QCOMPARE(threeDDetectModelKind(QByteArray("not a model at all")), ThreeDModelKind::Unknown);
        QCOMPARE(threeDDetectModelKind(QByteArray()), ThreeDModelKind::Unknown);
    }

    // ---- 嵌入与读回 ----

    void aU3dModelSurvivesTheRoundTrip() {
        QString error;
        ThreeDAnnotationSpec spec;
        spec.modelBytes = m_cubeU3d;
        spec.rect = QRectF(20, 30, 120, 90);
        const QByteArray written = writeThreeDAnnotation(onePagePdf(), spec, &error);
        QVERIFY2(!written.isEmpty(), qPrintable(error));

        const QVector<ThreeDAnnotationInfo> found = readThreeDAnnotations(written, 0, &error);
        QVERIFY2(!found.isEmpty(), qPrintable(error));
        QCOMPARE(found.size(), 1);
        const ThreeDAnnotationInfo& info = found.first();
        QCOMPARE(info.modelKind, ThreeDModelKind::U3D);
        QCOMPARE(info.modelMimeType, QStringLiteral("/U3D"));
        QCOMPARE(info.modelSize, static_cast<qint64>(m_cubeU3d.size()));
        QVERIFY(info.modelEmbedded);
        // 读回来要能报出网格规模与包围盒。
        QVERIFY(info.modelHasMesh);
        QCOMPARE(info.modelMeshCount, 1);
        QCOMPARE(info.modelVertexCount, 8);
        QCOMPARE(info.modelFaceCount, 12);
        QVERIFY(qAbs(info.modelBoundsMin.x()) < 1e-4);
        QVERIFY(qAbs(info.modelBoundsMin.y()) < 1e-4);
        QVERIFY(qAbs(info.modelBoundsMin.z()) < 1e-4);
        QVERIFY(qAbs(info.modelBoundsMax.x() - 1.0) < 1e-4);
        QVERIFY(qAbs(info.modelBoundsMax.y() - 1.0) < 1e-4);
        QVERIFY(qAbs(info.modelBoundsMax.z() - 1.0) < 1e-4);
    }

    void aPrcModelIsWrittenWithThePrcSubtype() {
        QString error;
        ThreeDAnnotationSpec spec;
        spec.modelBytes = buildPrc(QByteArray(), QByteArray());
        spec.rect = QRectF(10, 10, 100, 80);
        const QByteArray written = writeThreeDAnnotation(onePagePdf(), spec, &error);
        QVERIFY2(!written.isEmpty(), qPrintable(error));

        const QVector<ThreeDAnnotationInfo> found = readThreeDAnnotations(written, 0, &error);
        QVERIFY2(!found.isEmpty(), qPrintable(error));
        QCOMPARE(found.first().modelKind, ThreeDModelKind::Prc);
        QCOMPARE(found.first().modelMimeType, QStringLiteral("/PRC"));
    }

    void bytesThatAreNeitherU3dNorPrcAreRejected() {
        QString error;
        ThreeDAnnotationSpec spec;
        spec.modelBytes = QByteArray("FAKE-U3D-PRC-BYTES-");
        const QByteArray written = writeThreeDAnnotation(onePagePdf(), spec, &error);
        QVERIFY(written.isEmpty());
        QVERIFY2(error.contains(QStringLiteral("不像")), qPrintable(error));
    }

    void theModelBytesComeBackWithTheAnnotation() {
        QString error;
        ThreeDAnnotationSpec spec;
        spec.modelBytes = m_cubeU3d;
        spec.rect = QRectF(20, 30, 120, 90);
        const QByteArray written = writeThreeDAnnotation(onePagePdf(), spec, &error);
        QVERIFY2(!written.isEmpty(), qPrintable(error));

        const QVector<ThreeDAnnotationInfo> found = readThreeDAnnotations(written, 0, &error);
        QVERIFY2(!found.isEmpty(), qPrintable(error));
        // 表现层渲染要的是模型字节本身，所以读回来时得带上。
        QCOMPARE(found.first().modelData, m_cubeU3d);
    }

    void theFallbackPosterCanBeReadBackForThePresentationLayer() {
        QString error;
        ThreeDAnnotationSpec spec;
        spec.modelBytes = m_cubeU3d;
        spec.rect = QRectF(20, 30, 120, 90);
        aipdf::pdf::FallbackImage poster;
        poster.width = 3;
        poster.height = 2;
        poster.rgb = QByteArray(3 * 2 * 3, static_cast<char>(0x40));
        spec.fallback = poster;
        const QByteArray written = writeThreeDAnnotation(onePagePdf(), spec, &error);
        QVERIFY2(!written.isEmpty(), qPrintable(error));

        aipdf::pdf::FallbackImage readBack;
        QVERIFY2(aipdf::pdf::readThreeDFallbackImage(written, 0, 0, readBack, &error), qPrintable(error));
        QVERIFY(readBack.isValid());
        QCOMPARE(readBack.width, 3);
        QCOMPARE(readBack.height, 2);
        QCOMPARE(readBack.rgb.size(), 18);
        QCOMPARE(readBack.rgb, poster.rgb);
    }

    void theWrittenFileStillOpensWithTheSamePageCount() {        QString error;
        ThreeDAnnotationSpec spec;
        spec.modelBytes = m_cubeU3d;
        spec.rect = QRectF(20, 30, 120, 90);
        const QByteArray written = writeThreeDAnnotation(onePagePdf(), spec, &error);
        QVERIFY2(!written.isEmpty(), qPrintable(error));
        QCOMPARE(pageCountOf(written), 1);
    }

    // ---- 解码 ----

    void theCubeMeshDecodesToTheExpectedCounts() {
        QVector<ThreeDMesh> meshes;
        QString error;
        QStringList notes;
        QVERIFY2(threeDDecodeU3dMesh(m_cubeU3d, meshes, &error, &notes), qPrintable(error));
        QCOMPARE(meshes.size(), 1);
        const ThreeDMesh& mesh = meshes.first();
        QCOMPARE(mesh.vertexCount(), 8);
        QCOMPARE(mesh.faceCount(), 12);
        QCOMPARE(mesh.faceIndices.size(), 36);
        // 立方体没带法线时，法线下标必须标成 -1，不能留 0 蒙混过关。
        QVERIFY(!mesh.faceNormalIndices.isEmpty());
        for (int index : mesh.faceNormalIndices) QCOMPARE(index, -1);
        for (int index : mesh.faceColorIndices) QCOMPARE(index, -1);
        QVERIFY(!mesh.hasNormals());
        QVERIFY(!mesh.hasColors());
        const QVector3D boundsMin = mesh.boundsMin();
        const QVector3D boundsMax = mesh.boundsMax();
        QVERIFY(qAbs(boundsMin.x()) < 1e-5);
        QVERIFY(qAbs(boundsMax.z() - 1.0) < 1e-5);
    }

    void theDetailedCubeDecodesWithNormalsAndColors() {
        QVector<ThreeDMesh> meshes;
        QString error;
        QVERIFY2(threeDDecodeU3dMesh(m_detailedU3d, meshes, &error), qPrintable(error));
        QCOMPARE(meshes.size(), 1);
        const ThreeDMesh& mesh = meshes.first();
        QCOMPARE(mesh.vertexCount(), 54);
        QCOMPARE(mesh.faceCount(), 48);
        QVERIFY(mesh.hasNormals());
        QVERIFY(mesh.hasColors());
        QCOMPARE(mesh.normals.size(), 54);
        QCOMPARE(mesh.colors.size(), 54);
        // 逐角下标要在范围内。
        for (int index = 0; index < mesh.faceIndices.size(); ++index) {
            QVERIFY(mesh.faceIndices.at(index) >= 0);
            QVERIFY(mesh.faceIndices.at(index) < mesh.positions.size());
            QVERIFY(mesh.faceNormalIndices.at(index) >= 0);
            QVERIFY(mesh.faceNormalIndices.at(index) < mesh.normals.size());
            QVERIFY(mesh.faceColorIndices.at(index) >= 0);
            QVERIFY(mesh.faceColorIndices.at(index) < mesh.colors.size());
        }
    }

    void aCompressedU3dIsRejectedWithAReason() {
        const QByteArray compressed = buildU3d({m_detailedCube}, true);
        QVector<ThreeDMesh> meshes;
        QString error;
        QVERIFY(!threeDDecodeU3dMesh(compressed, meshes, &error));
        QVERIFY2(error.contains(QStringLiteral("压缩")), qPrintable(error));

        // 摘要里也要看得出是「解不出来」而不是「文件里没有模型」。
        ThreeDModelInfo info;
        QVERIFY(!threeDInspectModel(compressed, info));
        QVERIFY(!info.error.isEmpty());
        QVERIFY(!info.hasMesh);
    }

    // ---- 相机 ----

    void theCameraRotationIsAPureFunction() {
        const QVector3D xAxis(1.0, 0.0, 0.0);
        const QVector3D rotated = threeDRotateY(xAxis, 90.0);
        QVERIFY(qAbs(rotated.x()) < 1e-6);
        QVERIFY(qAbs(rotated.y()) < 1e-6);
        QVERIFY(qAbs(rotated.z() + 1.0) < 1e-6);
        const QVector3D tilted = threeDRotateX(QVector3D(0.0, 1.0, 0.0), 90.0);
        QVERIFY(qAbs(tilted.x()) < 1e-6);
        QVERIFY(qAbs(tilted.y()) < 1e-6);
        QVERIFY(qAbs(tilted.z() - 1.0) < 1e-6);
        // 转 360° 回到原处。
        const QVector3D full = threeDRotateY(xAxis, 360.0);
        QVERIFY(qAbs(full.x() - 1.0) < 1e-6);
        // 缩放要夹住：0 与负数都不该放行。
        QCOMPARE(threeDClampZoom(0.0), 1.0);
        QCOMPARE(threeDClampZoom(-3.0), 1.0);
        QVERIFY(threeDClampZoom(1000.0) <= 10.0);
        QVERIFY(threeDClampZoom(0.0001) >= 0.1);
    }

    // ---- 渲染 ----

    void theModelRendersIntoANonEmptyImage() {
        QVector<ThreeDMesh> meshes;
        QString error;
        QVERIFY2(threeDDecodeU3dMesh(m_detailedU3d, meshes, &error), qPrintable(error));

        ThreeDRenderOptions options;
        options.width = 160;
        options.height = 120;
        ThreeDOrbitCamera camera;
        camera.azimuthDeg = 35.0;
        camera.elevationDeg = 25.0;
        const ThreeDRenderResult result = aipdf::pdf::threeDRenderMesh(meshes, camera, options);
        QVERIFY2(result.ok, qPrintable(result.error));
        QCOMPARE(result.source, ThreeDRenderSource::Model);
        QVERIFY(threeDRenderSourceIsModel(result.source));
        QCOMPARE(result.image.width(), 160);
        QCOMPARE(result.image.height(), 120);
        // 画面上不能只有背景色。
        QVERIFY(nonBackgroundPixelCount(result.image, options.background) > 500);
    }

    void rotatingByNinetyDegreesChangesThePicture() {
        QVector<ThreeDMesh> meshes;
        QString error;
        QVERIFY2(threeDDecodeU3dMesh(m_detailedU3d, meshes, &error), qPrintable(error));

        ThreeDRenderOptions options;
        options.width = 160;
        options.height = 120;
        ThreeDOrbitCamera camera;
        camera.azimuthDeg = 0.0;
        camera.elevationDeg = 0.0;
        const ThreeDRenderResult front = aipdf::pdf::threeDRenderMesh(meshes, camera, options);
        QVERIFY(front.ok);
        camera.azimuthDeg = 90.0;
        const ThreeDRenderResult side = aipdf::pdf::threeDRenderMesh(meshes, camera, options);
        QVERIFY(side.ok);

        const int changed = differentPixelCount(front.image, side.image);
        QVERIFY2(changed > 200, qPrintable(QStringLiteral("转 90° 后只有 %1 个像素不一样").arg(changed)));
    }

    void theProjectedBoundsFollowTheExpectedAspect() {
        // 夹具是 2×1×1 的长方体：正对着看宽高比 2，转 90° 之后变 1。
        QVector<ThreeDMesh> meshes;
        QString error;
        QVERIFY2(threeDDecodeU3dMesh(m_slabU3d, meshes, &error), qPrintable(error));

        ThreeDRenderOptions options;
        options.width = 200;
        options.height = 200;
        ThreeDOrbitCamera camera;
        camera.azimuthDeg = 0.0;
        camera.elevationDeg = 0.0;
        const ThreeDRenderResult front = aipdf::pdf::threeDRenderMesh(meshes, camera, options);
        QVERIFY(front.ok);
        const double frontAspect = front.projectedBounds.width() / front.projectedBounds.height();
        QVERIFY2(qAbs(frontAspect - 2.0) < 0.05, qPrintable(QStringLiteral("正面宽高比 %1").arg(frontAspect)));

        camera.azimuthDeg = 90.0;
        const ThreeDRenderResult side = aipdf::pdf::threeDRenderMesh(meshes, camera, options);
        QVERIFY(side.ok);
        const double sideAspect = side.projectedBounds.width() / side.projectedBounds.height();
        QVERIFY2(qAbs(sideAspect - 1.0) < 0.05, qPrintable(QStringLiteral("侧面宽高比 %1").arg(sideAspect)));
    }

    // ---- 表现层兜底 ----

    void thePosterPathIsTakenWhenThereIsNoModel() {
        const ThreeDRenderResult result = threeDRenderModel(QByteArray("garbage bytes"), m_poster,
                                                            ThreeDOrbitCamera(), ThreeDRenderOptions());
        QVERIFY2(result.ok, qPrintable(result.error));
        // 关键：走的是海报，不是模型。接口必须能分辨。
        QCOMPARE(result.source, ThreeDRenderSource::Poster);
        QVERIFY(!threeDRenderSourceIsModel(result.source));
        QVERIFY(threeDRenderSourceName(result.source).contains(QStringLiteral("非模型")));
    }

    void thePosterPathIsDistinguishableFromTheModelPath() {
        ThreeDRenderOptions options;
        options.width = 120;
        options.height = 90;
        const ThreeDRenderResult posterResult = threeDRenderModel(QByteArray(), m_poster,
                                                                  ThreeDOrbitCamera(), options);
        const ThreeDRenderResult modelResult = threeDRenderModel(m_cubeU3d, m_poster,
                                                                 ThreeDOrbitCamera(), options);
        QVERIFY(posterResult.ok);
        QVERIFY(modelResult.ok);
        QCOMPARE(posterResult.source, ThreeDRenderSource::Poster);
        QCOMPARE(modelResult.source, ThreeDRenderSource::Model);
        QVERIFY(posterResult.source != modelResult.source);
    }

    void thePosterCanBeRotatedAndScaled() {
        ThreeDRenderOptions options;
        options.width = 120;
        options.height = 90;
        ThreeDOrbitCamera camera;
        camera.azimuthDeg = 0.0;
        camera.elevationDeg = 0.0;
        const ThreeDRenderResult upright = threeDRenderModel(QByteArray(), m_poster, camera, options);
        QVERIFY(upright.ok);
        camera.azimuthDeg = 90.0;
        const ThreeDRenderResult turned = threeDRenderModel(QByteArray(), m_poster, camera, options);
        QVERIFY(turned.ok);
        // 海报是 2D 表现层：旋转 90° 之后画面必须不一样。
        QVERIFY(differentPixelCount(upright.image, turned.image) > 0);
        // 但它始终不是模型。
        QCOMPARE(turned.source, ThreeDRenderSource::Poster);
    }

    void aPrcWithAnEmbeddedSvgRendersTheSvg() {
        const QByteArray prc = buildPrc(QByteArray(), simpleSvg());
        const ThreeDRenderResult result = threeDRenderModel(prc, m_poster, ThreeDOrbitCamera(),
                                                            ThreeDRenderOptions());
        QVERIFY2(result.ok, qPrintable(result.error));
        QCOMPARE(result.source, ThreeDRenderSource::Svg);
        QVERIFY(!threeDRenderSourceIsModel(result.source));
    }

    void aPrcWithAnEmbeddedU3dRendersTheModel() {
        const QByteArray prc = buildPrc(m_cubeU3d, QByteArray());
        const ThreeDRenderResult result = threeDRenderModel(prc, m_poster, ThreeDOrbitCamera(),
                                                            ThreeDRenderOptions());
        QVERIFY2(result.ok, qPrintable(result.error));
        QCOMPARE(result.source, ThreeDRenderSource::Model);
        QVERIFY(threeDRenderSourceIsModel(result.source));
    }

    void withoutModelNorPosterThereIsNothingToShow() {
        const ThreeDRenderResult result = threeDRenderModel(QByteArray("garbage"), ThreeDPosterImage(),
                                                            ThreeDOrbitCamera(), ThreeDRenderOptions());
        QVERIFY(!result.ok);
        QVERIFY(!result.error.isEmpty());
        QCOMPARE(result.source, ThreeDRenderSource::None);
    }

private:
    MeshFixture m_cube;
    MeshFixture m_detailedCube;
    MeshFixture m_slab;
    QByteArray m_cubeU3d;
    QByteArray m_detailedU3d;
    QByteArray m_slabU3d;
    ThreeDPosterImage m_poster;
};

void Wave213dTests::initTestCase() {
    initPdfium();
    m_cube = makeBox(QVector3D(0, 0, 0), QVector3D(1, 1, 1), QColor(200, 200, 210), false);
    m_detailedCube = makeDetailedCube();
    m_slab = makeBox(QVector3D(-1, -0.5, -0.5), QVector3D(1, 0.5, 0.5), QColor(150, 180, 210));
    m_cubeU3d = buildU3d({m_cube});
    m_detailedU3d = buildU3d({m_detailedCube});
    m_slabU3d = buildU3d({m_slab});
    m_poster = makePoster();
}

void Wave213dTests::cleanupTestCase() {
    m_cubeU3d.clear();
    m_detailedU3d.clear();
    m_slabU3d.clear();
}

QTEST_MAIN(Wave213dTests)
#include "Wave213dTests.moc"
