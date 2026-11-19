#include "pdf/annot/ThreeDModel.h"

#include <QPainter>
#include <QSvgRenderer>
#include <QTransform>

#include <cmath>
#include <limits>

namespace aipdf::pdf {
namespace {

// 渲染用的一个顶点：模型空间的位置、法线、颜色。
//
// 为什么把法线与颜色摊到每个角上，而不是留在顶点表里：CLOD 的面可以带
// 逐角法线与逐角颜色，摊平之后光栅化器不用关心索引怎么组织。
struct RenderVertex {
    QVector3D position;
    QVector3D normal;
    QColor color;
};

// 一个三角形。
struct RenderTriangle {
    RenderVertex vertices[3];
};

// 屏幕上的顶点：像素坐标 + 深度。
struct ScreenVertex {
    double x = 0.0;
    double y = 0.0;
    double depth = 0.0;     // 到相机的距离，越小越近
    double invDepth = 0.0;  // 1/深度，透视插值要用
    QVector3D viewPosition; // 视图空间坐标，算光照方向要用
};

// 把网格摊平成三角形。
//
// 为什么面要整个丢掉而不是只丢一个角：少一个角的三角形画出来是错的，
// 比不画更糟。
QVector<RenderTriangle> flattenMesh(const ThreeDMesh& mesh, const QColor& defaultColor) {
    QVector<RenderTriangle> triangles;
    const int faceCount = mesh.faceCount();
    triangles.reserve(faceCount);
    for (int face = 0; face < faceCount; ++face) {
        RenderTriangle triangle;
        bool valid = true;
        for (int corner = 0; corner < 3; ++corner) {
            const int index = face * 3 + corner;
            const int positionIndex = mesh.faceIndices.value(index, -1);
            if (positionIndex < 0 || positionIndex >= mesh.positions.size()) {
                valid = false;
                break;
            }
            RenderVertex& vertex = triangle.vertices[corner];
            vertex.position = mesh.positions.at(positionIndex);
            const int normalIndex = mesh.faceNormalIndices.value(index, -1);
            if (normalIndex >= 0 && normalIndex < mesh.normals.size()) {
                vertex.normal = mesh.normals.at(normalIndex);
            }
            const int colorIndex = mesh.faceColorIndices.value(index, -1);
            if (colorIndex >= 0 && colorIndex < mesh.colors.size()) {
                vertex.color = mesh.colors.at(colorIndex);
            } else {
                vertex.color = defaultColor;
            }
        }
        if (valid) triangles.append(triangle);
    }
    return triangles;
}

// 一个三角形在屏幕上的样子：像素坐标 + 深度 + 法线 + 颜色。
struct ShadedVertex {
    ScreenVertex screen;
    QVector3D normal;
    QColor color;
};

// 最小 z-buffer 光栅化器。
//
// 做法：边函数算重心坐标，深度缓冲存 1/深度（透视正确的插值），
// 颜色与法线按透视权重插值，光照是朗伯模型 + 环境光。
// 不引第三方渲染库是这个模块的硬要求——仓库里不塞新的第三方依赖。
void rasterizeTriangle(QImage& image, QVector<float>& depthBuffer, const ShadedVertex& v0,
                       const ShadedVertex& v1, const ShadedVertex& v2, const QVector3D& lightDirection,
                       double ambient) {
    const double signedArea = (v1.screen.x - v0.screen.x) * (v2.screen.y - v0.screen.y)
                               - (v2.screen.x - v0.screen.x) * (v1.screen.y - v0.screen.y);
    if (std::fabs(signedArea) < 1e-9) return;  // 投影后面积为零的退化三角形

    const int imageWidth = image.width();
    const int imageHeight = image.height();
    const int minX = qMax(0, static_cast<int>(std::floor(qMin(v0.screen.x, qMin(v1.screen.x, v2.screen.x)))));
    const int maxX = qMin(imageWidth - 1, static_cast<int>(std::ceil(qMax(v0.screen.x, qMax(v1.screen.x, v2.screen.x)))));
    const int minY = qMax(0, static_cast<int>(std::floor(qMin(v0.screen.y, qMin(v1.screen.y, v2.screen.y)))));
    const int maxY = qMin(imageHeight - 1, static_cast<int>(std::ceil(qMax(v0.screen.y, qMax(v1.screen.y, v2.screen.y)))));
    if (minX > maxX || minY > maxY) return;

    for (int y = minY; y <= maxY; ++y) {
        for (int x = minX; x <= maxX; ++x) {
            const double px = x + 0.5;
            const double py = y + 0.5;
            const double w0 = ((v1.screen.x - px) * (v2.screen.y - py)
                               - (v2.screen.x - px) * (v1.screen.y - py)) / signedArea;
            const double w1 = ((v2.screen.x - px) * (v0.screen.y - py)
                               - (v0.screen.x - px) * (v2.screen.y - py)) / signedArea;
            const double w2 = 1.0 - w0 - w1;
            if (w0 < 0.0 || w1 < 0.0 || w2 < 0.0) continue;  // 像素在三角形外

            const double invDepth = w0 * v0.screen.invDepth + w1 * v1.screen.invDepth
                                    + w2 * v2.screen.invDepth;
            if (invDepth <= 0.0) continue;
            const double depth = 1.0 / invDepth;
            const int pixel = y * imageWidth + x;
            if (depth >= depthBuffer.at(pixel)) continue;  // 后面来的被挡住

            // 法线：透视权重插值后再归一化。
            QVector3D normal = v0.normal * w0 + v1.normal * w1 + v2.normal * w2;
            if (normal.isNull()) {
                // 没有法线就用面法线兜底，否则平面会全黑。
                normal = QVector3D::crossProduct(v1.screen.viewPosition - v0.screen.viewPosition,
                                                 v2.screen.viewPosition - v0.screen.viewPosition);
            }
            if (!normal.isNull()) normal.normalize();

            // 双面光照：把法线翻向相机那一侧，背面不至于全黑。
            const QVector3D toCamera = QVector3D(0.0, 0.0, 1.0) - v0.screen.viewPosition.normalized();
            if (QVector3D::dotProduct(normal, toCamera) < 0.0) normal = -normal;

            const double lambert = qMax(0.0, QVector3D::dotProduct(normal, lightDirection));
            const double intensity = ambient + (1.0 - ambient) * lambert;

            // 颜色：透视权重插值（w/z 权重除以插值的 1/z）。
            const double baseRed = (v0.color.redF() * w0 * v0.screen.invDepth
                                    + v1.color.redF() * w1 * v1.screen.invDepth
                                    + v2.color.redF() * w2 * v2.screen.invDepth)
                                   / invDepth;
            const double baseGreen = (v0.color.greenF() * w0 * v0.screen.invDepth
                                      + v1.color.greenF() * w1 * v1.screen.invDepth
                                      + v2.color.greenF() * w2 * v2.screen.invDepth)
                                     / invDepth;
            const double baseBlue = (v0.color.blueF() * w0 * v0.screen.invDepth
                                     + v1.color.blueF() * w1 * v1.screen.invDepth
                                     + v2.color.blueF() * w2 * v2.screen.invDepth)
                                    / invDepth;
            const int red = qBound(0, static_cast<int>(baseRed * 255.0), 255);
            const int greenChannel = qBound(0, static_cast<int>(baseGreen * 255.0), 255);
            const int blueChannel = qBound(0, static_cast<int>(baseBlue * 255.0), 255);
            image.setPixel(x, y, qRgb(red, greenChannel, blueChannel));
            depthBuffer[pixel] = static_cast<float>(depth);
        }
    }
}

// 把 SVG 画到位图上（PRC 里的回退表示）。
//
// 为什么用 Qt 自带的 QSvgRenderer：仓库已经链接 Qt6::Svg，再塞一个第三方
// SVG 库没有意义。
ThreeDRenderResult renderSvgFallback(const QByteArray& svg, const ThreeDRenderOptions& options) {
    ThreeDRenderResult result;
    QSvgRenderer renderer(svg);
    if (!renderer.isValid()) {
        result.error = QStringLiteral("PRC 里的 SVG 画不出来（内容不是有效的 SVG）");
        return result;
    }
    const int width = qMax(1, options.width);
    const int height = qMax(1, options.height);
    QImage canvas(width, height, QImage::Format_RGB32);
    canvas.fill(options.background);
    QPainter painter(&canvas);
    renderer.render(&painter);
    painter.end();
    result.ok = true;
    result.source = ThreeDRenderSource::Svg;
    result.image = canvas;
    result.projectedBounds = QRectF(0.0, 0.0, width, height);
    result.notes.append(QStringLiteral("当前画面是 PRC 里的 SVG 回退表示（2D 表现层），不是 3D 模型"));
    return result;
}

}  // namespace

ThreeDRenderResult threeDRenderMesh(const QVector<ThreeDMesh>& meshes, const ThreeDOrbitCamera& camera,
                                     const ThreeDRenderOptions& options) {
    ThreeDRenderResult result;
    const int width = qMax(1, options.width);
    const int height = qMax(1, options.height);

    // 多部件模型（一个 U3D 里多个 CLOD 生成器）一起画。
    QVector<RenderTriangle> triangles;
    QVector3D boundsMin;
    QVector3D boundsMax;
    bool hasBounds = false;
    for (const ThreeDMesh& mesh : meshes) {
        const QVector<RenderTriangle> partTriangles = flattenMesh(mesh, options.defaultColor);
        triangles += partTriangles;
        for (const RenderTriangle& triangle : partTriangles) {
            for (const RenderVertex& vertex : triangle.vertices) {
                if (!hasBounds) {
                    boundsMin = vertex.position;
                    boundsMax = vertex.position;
                    hasBounds = true;
                    continue;
                }
                boundsMin.setX(qMin(boundsMin.x(), vertex.position.x()));
                boundsMin.setY(qMin(boundsMin.y(), vertex.position.y()));
                boundsMin.setZ(qMin(boundsMin.z(), vertex.position.z()));
                boundsMax.setX(qMax(boundsMax.x(), vertex.position.x()));
                boundsMax.setY(qMax(boundsMax.y(), vertex.position.y()));
                boundsMax.setZ(qMax(boundsMax.z(), vertex.position.z()));
            }
        }
    }
    if (triangles.isEmpty()) {
        result.error = QStringLiteral("模型里没有能画的三角形");
        return result;
    }

    // 轨道中心取包围盒中心：用户拖动时模型是绕着自己转的。
    const QVector3D center = (boundsMin + boundsMax) * 0.5;
    double radius = 0.0;
    for (const RenderTriangle& triangle : triangles) {
        for (const RenderVertex& vertex : triangle.vertices) {
            radius = qMax(radius, (vertex.position - center).length());
        }
    }
    if (radius <= 0.0) radius = 1.0;

    const double zoom = threeDClampZoom(camera.zoom);
    // 相机距离：默认在模型外 3 倍半径处；放大就往前挪（像推镜头）。
    const double distance = qMax(radius * 1.2, radius * 3.0 / zoom);
    const double fieldOfView = qBound(5.0, options.fieldOfView, 170.0);
    const double focal = 1.0 / std::tan(qDegreesToRadians(fieldOfView) * 0.5);
    const QVector3D lightDirection = options.lightDirection.isNull()
                                         ? QVector3D(0.0, 0.0, 1.0)
                                         : options.lightDirection.normalized();
    const double ambient = qBound(0.0, options.ambient, 1.0);

    QImage image(width, height, QImage::Format_RGB32);
    image.fill(options.background);
    QVector<float> depthBuffer(width * height, std::numeric_limits<float>::max());

    double minX = 0.0;
    double maxX = 0.0;
    double minY = 0.0;
    double maxY = 0.0;
    bool hasProjection = false;

    for (const RenderTriangle& triangle : triangles) {
        ShadedVertex shaded[3];
        bool behind = false;
        for (int corner = 0; corner < 3; ++corner) {
            const RenderVertex& vertex = triangle.vertices[corner];
            QVector3D view = vertex.position - center;
            view = threeDRotateY(view, camera.azimuthDeg);
            view = threeDRotateX(view, camera.elevationDeg);
            const double depth = distance - view.z();
            if (depth <= 1e-4) {
                behind = true;  // 顶点跑到相机后面了，这个三角形不画
                break;
            }
            const double ndcX = view.x() * focal / depth;
            const double ndcY = view.y() * focal / depth;
            shaded[corner].screen.x = (ndcX * 0.5 + 0.5) * width;
            shaded[corner].screen.y = (0.5 - ndcY * 0.5) * height;
            shaded[corner].screen.depth = depth;
            shaded[corner].screen.invDepth = 1.0 / depth;
            shaded[corner].screen.viewPosition = view;
            shaded[corner].normal = vertex.normal;
            shaded[corner].color = vertex.color;
            if (!hasProjection) {
                minX = maxX = shaded[corner].screen.x;
                minY = maxY = shaded[corner].screen.y;
                hasProjection = true;
            } else {
                minX = qMin(minX, shaded[corner].screen.x);
                maxX = qMax(maxX, shaded[corner].screen.x);
                minY = qMin(minY, shaded[corner].screen.y);
                maxY = qMax(maxY, shaded[corner].screen.y);
            }
        }
        if (behind) continue;
        rasterizeTriangle(image, depthBuffer, shaded[0], shaded[1], shaded[2], lightDirection, ambient);
    }

    if (!hasProjection) {
        result.error = QStringLiteral("模型整个跑到相机后面去了，画不出东西");
        return result;
    }
    result.ok = true;
    result.source = ThreeDRenderSource::Model;
    result.image = image;
    result.projectedBounds = QRectF(minX, minY, maxX - minX, maxY - minY);
    return result;
}

ThreeDRenderResult threeDRenderPoster(const ThreeDPosterImage& poster, const ThreeDOrbitCamera& camera,
                                       const ThreeDRenderOptions& options) {
    ThreeDRenderResult result;
    if (!poster.isValid()) {
        result.error = QStringLiteral("没有可用的降级海报");
        return result;
    }
    const int width = qMax(1, options.width);
    const int height = qMax(1, options.height);

    QImage source(poster.width, poster.height, QImage::Format_RGB888);
    for (int row = 0; row < poster.height; ++row) {
        std::memcpy(source.scanLine(row), poster.rgb.constData() + row * poster.width * 3,
                    static_cast<size_t>(poster.width) * 3);
    }

    QImage canvas(width, height, QImage::Format_RGB32);
    canvas.fill(options.background);

    // 海报只做 2D 变换：方位角当平面旋转角，俯仰角当竖向压扁，缩放直接乘。
    // 这不是 3D 模型旋转——结果里的 source 一定是 Poster。
    const double zoom = threeDClampZoom(camera.zoom);
    const double squash = qBound(0.05, std::cos(qDegreesToRadians(camera.elevationDeg)), 1.0);
    const double radians = qDegreesToRadians(camera.azimuthDeg);
    const double cosine = std::cos(radians);
    const double sine = std::sin(radians);
    // Qt 的变换是行向量约定：x' = m11*x + m21*y + dx。
    const double m11 = cosine * zoom;
    const double m12 = sine * zoom;
    const double m21 = -sine * zoom * squash;
    const double m22 = cosine * zoom * squash;
    const double halfWidth = poster.width * 0.5;
    const double halfHeight = poster.height * 0.5;
    const double dx = width * 0.5 - (halfWidth * m11 + halfHeight * m21);
    const double dy = height * 0.5 - (halfWidth * m12 + halfHeight * m22);
    const QTransform transform(m11, m12, m21, m22, dx, dy);

    QPainter painter(&canvas);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    // Qt 6 的 QPainter 没有 drawImage(QTransform, ...) 这一档了，
    // 得把变换挂到 painter 上再画。
    painter.setTransform(transform);
    painter.drawImage(QPointF(0.0, 0.0), source);
    painter.end();

    result.ok = true;
    result.source = ThreeDRenderSource::Poster;
    result.image = canvas;
    result.projectedBounds = QRectF(0.0, 0.0, width, height);
    result.notes.append(QStringLiteral("当前画面是注解的降级海报（2D 表现层），不是 3D 模型"));
    return result;
}

ThreeDRenderResult threeDRenderModel(const QByteArray& modelBytes, const ThreeDPosterImage& poster,
                                      const ThreeDOrbitCamera& camera, const ThreeDRenderOptions& options) {
    QStringList notes;
    const ThreeDModelKind kind = threeDDetectModelKind(modelBytes);

    if (kind == ThreeDModelKind::U3D) {
        QVector<ThreeDMesh> meshes;
        QString error;
        QStringList decodeNotes;
        if (threeDDecodeU3dMesh(modelBytes, meshes, &error, &decodeNotes) && !meshes.isEmpty()) {
            ThreeDRenderResult rendered = threeDRenderMesh(meshes, camera, options);
            rendered.source = ThreeDRenderSource::Model;
            rendered.notes = decodeNotes + rendered.notes;
            return rendered;
        }
        notes.append(QStringLiteral("U3D 没能解出网格：%1").arg(error));
    } else if (kind == ThreeDModelKind::Prc) {
        notes.append(QStringLiteral("PRC 容器：本程序不解析它自己的 B-rep 与装配树，"
                                    "只在里面找内嵌的 U3D 与 SVG"));
        const int offset = threeDFindEmbeddedU3dOffset(modelBytes);
        if (offset >= 0) {
            QVector<ThreeDMesh> meshes;
            QString error;
            QStringList decodeNotes;
            if (threeDDecodeU3dMesh(modelBytes.mid(offset), meshes, &error, &decodeNotes)
                && !meshes.isEmpty()) {
                ThreeDRenderResult rendered = threeDRenderMesh(meshes, camera, options);
                rendered.source = ThreeDRenderSource::Model;
                rendered.notes = notes + decodeNotes + rendered.notes;
                rendered.notes.append(QStringLiteral("网格来自 PRC 容器内嵌的 U3D"));
                return rendered;
            }
            notes.append(QStringLiteral("PRC 内嵌的 U3D 没能解出网格：%1").arg(error));
        }
        const QByteArray svg = threeDFindEmbeddedSvg(modelBytes);
        if (!svg.isEmpty()) {
            ThreeDRenderResult rendered = renderSvgFallback(svg, options);
            if (rendered.ok) {
                rendered.notes = notes + rendered.notes;
                return rendered;
            }
            notes.append(rendered.error);
        }
    } else if (!modelBytes.isEmpty()) {
        notes.append(QStringLiteral("模型文件头既不像 U3D（应以 \"U3D\\0\" 开头）"
                                    "也不像 PRC（应以 \"PRC\" 开头），没法当 3D 模型解"));
    }

    // 兜底：降级海报。海报能转能缩，但它不是模型——source 标死。
    if (poster.isValid()) {
        ThreeDRenderResult rendered = threeDRenderPoster(poster, camera, options);
        rendered.notes = notes + rendered.notes;
        return rendered;
    }

    ThreeDRenderResult result;
    result.notes = notes;
    result.error = notes.isEmpty()
                       ? QStringLiteral("既没有可渲染的模型，也没有降级海报")
                       : notes.join(QStringLiteral("；"));
    return result;
}

}  // namespace aipdf::pdf
