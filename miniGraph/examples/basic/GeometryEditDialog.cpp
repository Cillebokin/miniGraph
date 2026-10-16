#include "GeometryEditDialog.h"

#include "MiniGraph/CanvasGeometry.h"
#include "PolylineText.h"

#include <QCheckBox>
#include <QColor>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QPlainTextEdit>
#include <QStackedWidget>
#include <QVBoxLayout>

#include <cmath>

namespace
{

QString GetEntityTypeName(mini_graph::CanvasEntityType type)
{
    switch (type)
    {
    case mini_graph::CanvasEntityType::Point:
        return QObject::tr("Point");
    case mini_graph::CanvasEntityType::Line:
        return QObject::tr("Line");
    case mini_graph::CanvasEntityType::Arc:
        return QObject::tr("Arc");
    case mini_graph::CanvasEntityType::Circle:
        return QObject::tr("Circle");
    case mini_graph::CanvasEntityType::Polyline:
        return QObject::tr("Polyline");
    case mini_graph::CanvasEntityType::Invalid:
        break;
    }
    return QObject::tr("Invalid");
}

} // namespace

GeometryEditDialog::GeometryEditDialog(mini_graph::CanvasDocument *document,
                                       mini_graph::ItemId itemId,
                                       QWidget *parent)
    : QDialog(parent)
    , m_Document(document)
    , m_ItemId(itemId)
{
    setWindowTitle(tr("Edit Geometry"));
    setModal(true);

    auto *mainLayout = new QVBoxLayout(this);
    auto *typeLayout = new QFormLayout;
    auto *typeLabel = new QLabel(this);
    typeLabel->setObjectName(QStringLiteral("geometryEditTypeLabel"));
    typeLayout->addRow(tr("Type"), typeLabel);
    mainLayout->addLayout(typeLayout);

    m_ParameterStack = new QStackedWidget(this);
    m_ParameterStack->addWidget(CreatePointPage());
    m_ParameterStack->addWidget(CreateLinePage());
    m_ParameterStack->addWidget(CreateArcPage());
    m_ParameterStack->addWidget(CreateCirclePage());
    m_ParameterStack->addWidget(CreatePolylinePage());
    mainLayout->addWidget(m_ParameterStack);

    auto *styleGroup = new QGroupBox(tr("Display style"), this);
    auto *styleLayout = new QFormLayout(styleGroup);
    m_StrokeColorLineEdit = new QLineEdit(styleGroup);
    m_StrokeColorLineEdit->setObjectName(QStringLiteral("editStrokeColorLineEdit"));
    m_SelectedStrokeColorLineEdit = new QLineEdit(styleGroup);
    m_SelectedStrokeColorLineEdit->setObjectName(
                QStringLiteral("editSelectedStrokeColorLineEdit"));
    m_StrokeWidthSpinBox = CreateSpinBox(styleGroup,
                                         QStringLiteral("editStrokeWidthSpinBox"));
    m_StrokeWidthSpinBox->setRange(0.0, 1000000000.0);
    m_PointSizeSpinBox = CreateSpinBox(styleGroup,
                                       QStringLiteral("editPointSizeSpinBox"));
    m_PointSizeSpinBox->setRange(0.000001, 1000000000.0);
    m_VisibleCheckBox = new QCheckBox(tr("Visible"), styleGroup);
    m_VisibleCheckBox->setObjectName(QStringLiteral("editVisibleCheckBox"));
    styleLayout->addRow(tr("Stroke color (#RRGGBB)"), m_StrokeColorLineEdit);
    styleLayout->addRow(tr("Selected color (#RRGGBB)"),
                        m_SelectedStrokeColorLineEdit);
    styleLayout->addRow(tr("Stroke width"), m_StrokeWidthSpinBox);
    styleLayout->addRow(tr("Point size"), m_PointSizeSpinBox);
    styleLayout->addRow(QString(), m_VisibleCheckBox);
    mainLayout->addWidget(styleGroup);

    m_ButtonBox = new QDialogButtonBox(QDialogButtonBox::Ok
                                       | QDialogButtonBox::Cancel,
                                       this);
    m_ButtonBox->setObjectName(QStringLiteral("geometryEditDialogButtonBox"));
    m_ButtonBox->button(QDialogButtonBox::Ok)->setText(tr("Apply"));
    connect(m_ButtonBox,
            &QDialogButtonBox::accepted,
            this,
            &GeometryEditDialog::OnApplyRequested);
    connect(m_ButtonBox,
            &QDialogButtonBox::rejected,
            this,
            &QDialog::reject);
    mainLayout->addWidget(m_ButtonBox);

    const bool loaded = LoadItem();
    typeLabel->setText(GetEntityTypeName(m_Type));
    if (!loaded)
    {
        m_ButtonBox->button(QDialogButtonBox::Ok)->setEnabled(false);
    }
    resize(480, 560);
}

mini_graph::ItemId GeometryEditDialog::GetItemId() const
{
    return m_ItemId;
}

QDoubleSpinBox *GeometryEditDialog::CreateSpinBox(QWidget *parent,
                                                   const QString &objectName)
{
    auto *spinBox = new QDoubleSpinBox(parent);
    spinBox->setObjectName(objectName);
    spinBox->setRange(-1000000000.0, 1000000000.0);
    spinBox->setDecimals(9);
    spinBox->setSingleStep(1.0);
    return spinBox;
}

QWidget *GeometryEditDialog::CreatePointPage()
{
    auto *page = new QWidget(this);
    auto *layout = new QFormLayout(page);
    m_PointXSpinBox = CreateSpinBox(page, QStringLiteral("editPointXSpinBox"));
    m_PointYSpinBox = CreateSpinBox(page, QStringLiteral("editPointYSpinBox"));
    layout->addRow(tr("X"), m_PointXSpinBox);
    layout->addRow(tr("Y"), m_PointYSpinBox);
    return page;
}

QWidget *GeometryEditDialog::CreateLinePage()
{
    auto *page = new QWidget(this);
    auto *layout = new QFormLayout(page);
    m_LineStartXSpinBox = CreateSpinBox(page,
                                        QStringLiteral("editLineStartXSpinBox"));
    m_LineStartYSpinBox = CreateSpinBox(page,
                                        QStringLiteral("editLineStartYSpinBox"));
    m_LineEndXSpinBox = CreateSpinBox(page,
                                      QStringLiteral("editLineEndXSpinBox"));
    m_LineEndYSpinBox = CreateSpinBox(page,
                                      QStringLiteral("editLineEndYSpinBox"));
    layout->addRow(tr("Start X"), m_LineStartXSpinBox);
    layout->addRow(tr("Start Y"), m_LineStartYSpinBox);
    layout->addRow(tr("End X"), m_LineEndXSpinBox);
    layout->addRow(tr("End Y"), m_LineEndYSpinBox);
    return page;
}

QWidget *GeometryEditDialog::CreateArcPage()
{
    auto *page = new QWidget(this);
    auto *layout = new QFormLayout(page);
    m_ArcCenterXSpinBox = CreateSpinBox(page,
                                        QStringLiteral("editArcCenterXSpinBox"));
    m_ArcCenterYSpinBox = CreateSpinBox(page,
                                        QStringLiteral("editArcCenterYSpinBox"));
    m_ArcRadiusSpinBox = CreateSpinBox(page,
                                       QStringLiteral("editArcRadiusSpinBox"));
    m_ArcStartAngleSpinBox = CreateSpinBox(
                page,
                QStringLiteral("editArcStartAngleSpinBox"));
    m_ArcEndAngleSpinBox = CreateSpinBox(
                page,
                QStringLiteral("editArcEndAngleSpinBox"));
    m_ArcReversedCheckBox = new QCheckBox(tr("Clockwise"), page);
    m_ArcReversedCheckBox->setObjectName(QStringLiteral("editArcReversedCheckBox"));
    layout->addRow(tr("Center X"), m_ArcCenterXSpinBox);
    layout->addRow(tr("Center Y"), m_ArcCenterYSpinBox);
    layout->addRow(tr("Radius"), m_ArcRadiusSpinBox);
    layout->addRow(tr("Start angle (rad)"), m_ArcStartAngleSpinBox);
    layout->addRow(tr("End angle (rad)"), m_ArcEndAngleSpinBox);
    layout->addRow(QString(), m_ArcReversedCheckBox);
    return page;
}

QWidget *GeometryEditDialog::CreateCirclePage()
{
    auto *page = new QWidget(this);
    auto *layout = new QFormLayout(page);
    m_CircleCenterXSpinBox = CreateSpinBox(
                page,
                QStringLiteral("editCircleCenterXSpinBox"));
    m_CircleCenterYSpinBox = CreateSpinBox(
                page,
                QStringLiteral("editCircleCenterYSpinBox"));
    m_CircleRadiusSpinBox = CreateSpinBox(
                page,
                QStringLiteral("editCircleRadiusSpinBox"));
    layout->addRow(tr("Center X"), m_CircleCenterXSpinBox);
    layout->addRow(tr("Center Y"), m_CircleCenterYSpinBox);
    layout->addRow(tr("Radius"), m_CircleRadiusSpinBox);
    return page;
}

QWidget *GeometryEditDialog::CreatePolylinePage()
{
    auto *page = new QWidget(this);
    auto *layout = new QFormLayout(page);
    m_PolylineVerticesTextEdit = new QPlainTextEdit(page);
    m_PolylineVerticesTextEdit->setObjectName(
                QStringLiteral("editPolylineVerticesTextEdit"));
    m_PolylineClosedCheckBox = new QCheckBox(tr("Closed"), page);
    m_PolylineClosedCheckBox->setObjectName(
                QStringLiteral("editPolylineClosedCheckBox"));
    layout->addRow(tr("Vertices (x,y per line)"), m_PolylineVerticesTextEdit);
    layout->addRow(QString(), m_PolylineClosedCheckBox);
    return page;
}

bool GeometryEditDialog::LoadItem()
{
    if (!m_Document)
    {
        return false;
    }

    m_Type = m_Document->GetItemType(m_ItemId);
    if (m_Type == mini_graph::CanvasEntityType::Invalid)
    {
        return false;
    }

    switch (m_Type)
    {
    case mini_graph::CanvasEntityType::Point:
    {
        mini_graph::StruPointGeometry geometry;
        if (!m_Document->GetPointGeometry(m_ItemId, &geometry))
        {
            return false;
        }
        m_PointXSpinBox->setValue(geometry.position.GetX());
        m_PointYSpinBox->setValue(geometry.position.GetY());
        break;
    }
    case mini_graph::CanvasEntityType::Line:
    {
        mini_graph::StruLineGeometry geometry;
        if (!m_Document->GetLineGeometry(m_ItemId, &geometry))
        {
            return false;
        }
        m_LineStartXSpinBox->setValue(geometry.startPoint.GetX());
        m_LineStartYSpinBox->setValue(geometry.startPoint.GetY());
        m_LineEndXSpinBox->setValue(geometry.endPoint.GetX());
        m_LineEndYSpinBox->setValue(geometry.endPoint.GetY());
        break;
    }
    case mini_graph::CanvasEntityType::Arc:
    {
        mini_graph::StruArcGeometry geometry;
        if (!m_Document->GetArcGeometry(m_ItemId, &geometry))
        {
            return false;
        }
        m_ArcCenterXSpinBox->setValue(geometry.center.GetX());
        m_ArcCenterYSpinBox->setValue(geometry.center.GetY());
        m_ArcRadiusSpinBox->setValue(geometry.radius);
        m_ArcStartAngleSpinBox->setValue(geometry.startAngle);
        m_ArcEndAngleSpinBox->setValue(geometry.endAngle);
        m_ArcReversedCheckBox->setChecked(geometry.reversed);
        break;
    }
    case mini_graph::CanvasEntityType::Circle:
    {
        mini_graph::StruCircleGeometry geometry;
        if (!m_Document->GetCircleGeometry(m_ItemId, &geometry))
        {
            return false;
        }
        m_CircleCenterXSpinBox->setValue(geometry.center.GetX());
        m_CircleCenterYSpinBox->setValue(geometry.center.GetY());
        m_CircleRadiusSpinBox->setValue(geometry.radius);
        break;
    }
    case mini_graph::CanvasEntityType::Polyline:
    {
        mini_graph::StruPolylineGeometry geometry;
        if (!m_Document->GetPolylineGeometry(m_ItemId, &geometry))
        {
            return false;
        }
        m_PolylineVerticesTextEdit->setPlainText(
                    basic_example::FormatPolylineText(geometry.vertices));
        m_PolylineClosedCheckBox->setChecked(geometry.closed);
        break;
    }
    case mini_graph::CanvasEntityType::Invalid:
        return false;
    }

    mini_graph::StruCanvasStyle style;
    if (!m_Document->GetItemStyle(m_ItemId, &style))
    {
        return false;
    }
    m_StrokeColorLineEdit->setText(style.strokeColor.name());
    m_SelectedStrokeColorLineEdit->setText(style.selectedStrokeColor.name());
    m_StrokeWidthSpinBox->setValue(style.strokeWidth);
    m_PointSizeSpinBox->setValue(style.pointSize);
    m_VisibleCheckBox->setChecked(style.visible);
    m_ParameterStack->setCurrentIndex(static_cast<int>(m_Type) - 1);
    return true;
}

bool GeometryEditDialog::ReadStyle(mini_graph::StruCanvasStyle *style) const
{
    if (!style || !m_Document)
    {
        return false;
    }

    if (!m_Document->GetItemStyle(m_ItemId, style))
    {
        return false;
    }

    QColor strokeColor;
    strokeColor.setNamedColor(m_StrokeColorLineEdit->text().trimmed());
    QColor selectedStrokeColor;
    selectedStrokeColor.setNamedColor(
                m_SelectedStrokeColorLineEdit->text().trimmed());
    if (!strokeColor.isValid() || !selectedStrokeColor.isValid()
            || !std::isfinite(static_cast<double>(m_StrokeWidthSpinBox->value()))
            || m_StrokeWidthSpinBox->value() < 0.0
            || !std::isfinite(static_cast<double>(m_PointSizeSpinBox->value()))
            || m_PointSizeSpinBox->value() <= 0.0)
    {
        return false;
    }

    style->strokeColor = strokeColor;
    style->selectedStrokeColor = selectedStrokeColor;
    style->strokeWidth = m_StrokeWidthSpinBox->value();
    style->pointSize = m_PointSizeSpinBox->value();
    style->visible = m_VisibleCheckBox->isChecked();
    return true;
}

bool GeometryEditDialog::ApplyGeometry()
{
    if (!m_Document)
    {
        return false;
    }

    switch (m_Type)
    {
    case mini_graph::CanvasEntityType::Point:
    {
        mini_graph::StruPointGeometry geometry;
        geometry.position = mini_graph::CanvasVector(m_PointXSpinBox->value(),
                                                      m_PointYSpinBox->value());
        m_Document->SetPointGeometry(m_ItemId, geometry);
        return m_Document->GetItemType(m_ItemId) == m_Type;
    }
    case mini_graph::CanvasEntityType::Line:
    {
        mini_graph::StruLineGeometry geometry;
        geometry.startPoint = mini_graph::CanvasVector(m_LineStartXSpinBox->value(),
                                                        m_LineStartYSpinBox->value());
        geometry.endPoint = mini_graph::CanvasVector(m_LineEndXSpinBox->value(),
                                                      m_LineEndYSpinBox->value());
        m_Document->SetLineGeometry(m_ItemId, geometry);
        return m_Document->GetItemType(m_ItemId) == m_Type;
    }
    case mini_graph::CanvasEntityType::Arc:
    {
        mini_graph::StruArcGeometry geometry;
        geometry.center = mini_graph::CanvasVector(m_ArcCenterXSpinBox->value(),
                                                    m_ArcCenterYSpinBox->value());
        geometry.radius = m_ArcRadiusSpinBox->value();
        geometry.startAngle = m_ArcStartAngleSpinBox->value();
        geometry.endAngle = m_ArcEndAngleSpinBox->value();
        geometry.reversed = m_ArcReversedCheckBox->isChecked();
        if (!mini_graph::IsValidGeometry(geometry))
        {
            ShowInvalidInput(tr("The arc parameters are invalid."));
            return false;
        }
        m_Document->SetArcGeometry(m_ItemId, geometry);
        return m_Document->GetItemType(m_ItemId) == m_Type;
    }
    case mini_graph::CanvasEntityType::Circle:
    {
        mini_graph::StruCircleGeometry geometry;
        geometry.center = mini_graph::CanvasVector(m_CircleCenterXSpinBox->value(),
                                                    m_CircleCenterYSpinBox->value());
        geometry.radius = m_CircleRadiusSpinBox->value();
        if (!mini_graph::IsValidGeometry(geometry))
        {
            ShowInvalidInput(tr("The circle parameters are invalid."));
            return false;
        }
        m_Document->SetCircleGeometry(m_ItemId, geometry);
        return m_Document->GetItemType(m_ItemId) == m_Type;
    }
    case mini_graph::CanvasEntityType::Polyline:
    {
        mini_graph::StruPolylineGeometry geometry;
        if (!basic_example::ParsePolylineText(
                    m_PolylineVerticesTextEdit->toPlainText(),
                    &geometry.vertices))
        {
            ShowInvalidInput(tr("Enter at least two valid x,y vertices."));
            return false;
        }
        geometry.closed = m_PolylineClosedCheckBox->isChecked();
        if (!mini_graph::IsValidGeometry(geometry))
        {
            ShowInvalidInput(tr("The polyline parameters are invalid."));
            return false;
        }
        return m_Document->SetPolylineGeometry(m_ItemId, geometry);
    }
    case mini_graph::CanvasEntityType::Invalid:
        break;
    }
    return false;
}

void GeometryEditDialog::ShowInvalidInput(const QString &message)
{
    QMessageBox::warning(this, tr("Edit geometry"), message);
}

void GeometryEditDialog::OnApplyRequested()
{
    mini_graph::StruCanvasStyle style;
    if (!ReadStyle(&style))
    {
        ShowInvalidInput(tr("The display style is invalid."));
        return;
    }

    if (!ApplyGeometry())
    {
        return;
    }

    if (m_Document)
    {
        m_Document->SetItemStyle(m_ItemId, style);
    }
    accept();
}
