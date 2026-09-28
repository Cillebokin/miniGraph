#pragma once

#include "MiniGraph/CanvasDocument.h"

#include <QDialog>
#include <QPointer>

class QCheckBox;
class QDialogButtonBox;
class QDoubleSpinBox;
class QLineEdit;
class QPlainTextEdit;
class QStackedWidget;
class QWidget;

// Geometry property editor used by the example application.
// This class belongs to the caller layer and is not a MiniGraph dependency.
class GeometryEditDialog : public QDialog
{
    Q_OBJECT

public:
    // Edit the geometry and display style of one item.
    explicit GeometryEditDialog(mini_graph::CanvasDocument *document,
                                mini_graph::ItemId itemId,
                                QWidget *parent = nullptr);

    // Return the item identifier currently being edited.
    mini_graph::ItemId GetItemId() const;

private slots:
    // Validate and submit the dialog changes.
    void OnApplyRequested();

private:
    // Create a shared geometry parameter spin box.
    static QDoubleSpinBox *CreateSpinBox(QWidget *parent,
                                         const QString &objectName);

    // Create the point, line, arc, circle, and polyline parameter pages.
    QWidget *CreatePointPage();
    QWidget *CreateLinePage();
    QWidget *CreateArcPage();
    QWidget *CreateCirclePage();
    QWidget *CreatePolylinePage();

    // Load the current item data from the document.
    bool LoadItem();

    // Read and validate the style input.
    bool ReadStyle(mini_graph::StruCanvasStyle *style) const;

    // Validate and write the current item's geometry.
    bool ApplyGeometry();

    // Show an invalid input message.
    void ShowInvalidInput(const QString &message);

    // Document being edited.
    QPointer<mini_graph::CanvasDocument> m_Document;

    // Item identifier being edited.
    mini_graph::ItemId m_ItemId = 0;

    // Type of the item being edited.
    mini_graph::CanvasEntityType m_Type = mini_graph::CanvasEntityType::Invalid;

    // Geometry parameter page container.
    QStackedWidget *m_ParameterStack = nullptr;

    // Point parameter inputs.
    QDoubleSpinBox *m_PointXSpinBox = nullptr;
    QDoubleSpinBox *m_PointYSpinBox = nullptr;

    // Line parameter inputs.
    QDoubleSpinBox *m_LineStartXSpinBox = nullptr;
    QDoubleSpinBox *m_LineStartYSpinBox = nullptr;
    QDoubleSpinBox *m_LineEndXSpinBox = nullptr;
    QDoubleSpinBox *m_LineEndYSpinBox = nullptr;

    // Arc parameter inputs.
    QDoubleSpinBox *m_ArcCenterXSpinBox = nullptr;
    QDoubleSpinBox *m_ArcCenterYSpinBox = nullptr;
    QDoubleSpinBox *m_ArcRadiusSpinBox = nullptr;
    QDoubleSpinBox *m_ArcStartAngleSpinBox = nullptr;
    QDoubleSpinBox *m_ArcEndAngleSpinBox = nullptr;
    QCheckBox *m_ArcReversedCheckBox = nullptr;

    // Circle parameter inputs.
    QDoubleSpinBox *m_CircleCenterXSpinBox = nullptr;
    QDoubleSpinBox *m_CircleCenterYSpinBox = nullptr;
    QDoubleSpinBox *m_CircleRadiusSpinBox = nullptr;

    // Polyline vertex and closed-state inputs.
    QPlainTextEdit *m_PolylineVerticesTextEdit = nullptr;
    QCheckBox *m_PolylineClosedCheckBox = nullptr;

    // Common display style inputs.
    QLineEdit *m_StrokeColorLineEdit = nullptr;
    QLineEdit *m_SelectedStrokeColorLineEdit = nullptr;
    QDoubleSpinBox *m_StrokeWidthSpinBox = nullptr;
    QDoubleSpinBox *m_PointSizeSpinBox = nullptr;
    QCheckBox *m_VisibleCheckBox = nullptr;

    // Dialog buttons.
    QDialogButtonBox *m_ButtonBox = nullptr;
};
