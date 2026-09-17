#pragma once

#include "CanvasTransform.h"
#include "MiniGraph_global.h"

#include <QPointer>
#include <QOpenGLWidget>

#include <memory>

class QPainter;
class QMouseEvent;
class QWheelEvent;

namespace mini_graph {

class CanvasDocument;
class CanvasViewPrivate;

class MINIGRAPH_EXPORT CanvasView : public QOpenGLWidget
{
    Q_OBJECT
    Q_PROPERTY(bool gridVisible READ IsGridVisible WRITE SetGridVisible NOTIFY GridVisibleChanged)
    Q_PROPERTY(qreal gridSpacing READ GetGridSpacing WRITE SetGridSpacing NOTIFY GridSpacingChanged)
    Q_PROPERTY(qreal zoom READ GetZoom NOTIFY ZoomChanged)

public:
    explicit CanvasView(QWidget *parent = nullptr);
    ~CanvasView() override;

    CanvasDocument *GetDocument() const;
    void SetDocument(CanvasDocument *document);

    bool IsGridVisible() const;
    void SetGridVisible(bool visible);

    qreal GetGridSpacing() const;
    void SetGridSpacing(qreal spacing);

    qreal GetZoom() const;
    CanvasTransform GetTransform() const;
    void ResetView();

signals:
    void GridVisibleChanged(bool visible);
    void GridSpacingChanged(qreal spacing);
    void ZoomChanged(qreal zoom);

protected:
    void initializeGL() override;
    void resizeGL(int width, int height) override;
    void paintGL() override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;

private slots:
    void OnDocumentChanged();
    void CleanupGL();

private:
    void DrawGrid(QPainter &painter) const;
    void UploadLineBuffer();
    void DrawLines();

    QPointer<CanvasDocument> m_Document;
    QMetaObject::Connection m_DocumentConnection;
    CanvasTransform m_Transform;
    bool m_GridVisible;
    qreal m_GridSpacing;
    bool m_IsPanning;
    QPointF m_LastMousePosition;
    std::unique_ptr<CanvasViewPrivate> m_PrivateData;
};

} // namespace mini_graph
