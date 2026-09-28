#pragma once

#include "MiniGraph/CanvasDocument.h"

#include <QDialog>
#include <QPointer>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QPlainTextEdit;
class QStackedWidget;
class QWidget;

class GeometryCreateDialog : public QDialog
{
    Q_OBJECT

public:
    explicit GeometryCreateDialog(mini_graph::CanvasDocument *document,
                                  QWidget *parent = nullptr);

private slots:
    void OnCreateRequested();
    void OnGeometryTypeChanged(int index);

private:
    static QDoubleSpinBox *CreateSpinBox(QWidget *parent,
                                         const QString &objectName);

    QWidget *CreatePointPage();
    QWidget *CreateLinePage();
    QWidget *CreateArcPage();
    QWidget *CreateCirclePage();
    QWidget *CreatePolylinePage();
    bool CreateGeometry();

    QPointer<mini_graph::CanvasDocument> m_Document;
    QComboBox *m_TypeComboBox = nullptr;
    QStackedWidget *m_ParameterStack = nullptr;

    QDoubleSpinBox *m_PointXSpinBox = nullptr;
    QDoubleSpinBox *m_PointYSpinBox = nullptr;

    QDoubleSpinBox *m_LineStartXSpinBox = nullptr;
    QDoubleSpinBox *m_LineStartYSpinBox = nullptr;
    QDoubleSpinBox *m_LineEndXSpinBox = nullptr;
    QDoubleSpinBox *m_LineEndYSpinBox = nullptr;

    QDoubleSpinBox *m_ArcCenterXSpinBox = nullptr;
    QDoubleSpinBox *m_ArcCenterYSpinBox = nullptr;
    QDoubleSpinBox *m_ArcRadiusSpinBox = nullptr;
    QDoubleSpinBox *m_ArcStartAngleSpinBox = nullptr;
    QDoubleSpinBox *m_ArcEndAngleSpinBox = nullptr;
    QCheckBox *m_ArcReversedCheckBox = nullptr;

    QDoubleSpinBox *m_CircleCenterXSpinBox = nullptr;
    QDoubleSpinBox *m_CircleCenterYSpinBox = nullptr;
    QDoubleSpinBox *m_CircleRadiusSpinBox = nullptr;

    QPlainTextEdit *m_PolylineVerticesTextEdit = nullptr;
    QCheckBox *m_PolylineClosedCheckBox = nullptr;
};
