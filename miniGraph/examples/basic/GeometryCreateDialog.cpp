#include "GeometryCreateDialog.h"
#include "PolylineText.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QPlainTextEdit>
#include <QStackedWidget>
#include <QVBoxLayout>

GeometryCreateDialog::GeometryCreateDialog(mini_graph::CanvasDocument *document,
                                           QWidget *parent)
    : QDialog(parent)
    , m_Document(document)
{
    setWindowTitle(tr("Add Geometry"));
    setModal(true);

    auto *mainLayout = new QVBoxLayout(this);
    auto *typeLayout = new QHBoxLayout;
    auto *typeLabel = new QLabel(tr("Type"), this);
    m_TypeComboBox = new QComboBox(this);
    m_TypeComboBox->setObjectName(QStringLiteral("geometryTypeComboBox"));
    m_TypeComboBox->addItem(tr("Point"),
                            static_cast<int>(mini_graph::CanvasEntityType::Point));
    m_TypeComboBox->addItem(tr("Line"),
                            static_cast<int>(mini_graph::CanvasEntityType::Line));
    m_TypeComboBox->addItem(tr("Arc"),
                            static_cast<int>(mini_graph::CanvasEntityType::Arc));
    m_TypeComboBox->addItem(tr("Circle"),
                            static_cast<int>(mini_graph::CanvasEntityType::Circle));
    m_TypeComboBox->addItem(tr("Polyline"),
                            static_cast<int>(mini_graph::CanvasEntityType::Polyline));
    typeLayout->addWidget(typeLabel);
    typeLayout->addWidget(m_TypeComboBox, 1);
    mainLayout->addLayout(typeLayout);

    m_ParameterStack = new QStackedWidget(this);
    m_ParameterStack->addWidget(CreatePointPage());
    m_ParameterStack->addWidget(CreateLinePage());
    m_ParameterStack->addWidget(CreateArcPage());
    m_ParameterStack->addWidget(CreateCirclePage());
    m_ParameterStack->addWidget(CreatePolylinePage());
    mainLayout->addWidget(m_ParameterStack);

    auto *buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok
                                           | QDialogButtonBox::Cancel,
                                           this);
    buttonBox->setObjectName(QStringLiteral("geometryDialogButtonBox"));
    buttonBox->button(QDialogButtonBox::Ok)->setText(tr("Create"));
    connect(buttonBox,
            &QDialogButtonBox::accepted,
            this,
            &GeometryCreateDialog::OnCreateRequested);
    connect(buttonBox,
            &QDialogButtonBox::rejected,
            this,
            &QDialog::reject);
    mainLayout->addWidget(buttonBox);

    connect(m_TypeComboBox,
            static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged),
            this,
            &GeometryCreateDialog::OnGeometryTypeChanged);
    OnGeometryTypeChanged(m_TypeComboBox->currentIndex());
    resize(420, 360);
}

QDoubleSpinBox *GeometryCreateDialog::CreateSpinBox(QWidget *parent,
                                                    const QString &objectName)
{
    auto *spinBox = new QDoubleSpinBox(parent);
    spinBox->setObjectName(objectName);
    spinBox->setRange(-1000000000.0, 1000000000.0);
    spinBox->setDecimals(9);
    spinBox->setSingleStep(1.0);
    return spinBox;
}

QWidget *GeometryCreateDialog::CreatePointPage()
{
    auto *page = new QWidget(this);
    auto *layout = new QFormLayout(page);
    m_PointXSpinBox = CreateSpinBox(page, QStringLiteral("pointXSpinBox"));
    m_PointYSpinBox = CreateSpinBox(page, QStringLiteral("pointYSpinBox"));
    layout->addRow(tr("X"), m_PointXSpinBox);
    layout->addRow(tr("Y"), m_PointYSpinBox);
    return page;
}

QWidget *GeometryCreateDialog::CreateLinePage()
{
    auto *page = new QWidget(this);
    auto *layout = new QFormLayout(page);
    m_LineStartXSpinBox = CreateSpinBox(page, QStringLiteral("lineStartXSpinBox"));
    m_LineStartYSpinBox = CreateSpinBox(page, QStringLiteral("lineStartYSpinBox"));
    m_LineEndXSpinBox = CreateSpinBox(page, QStringLiteral("lineEndXSpinBox"));
    m_LineEndYSpinBox = CreateSpinBox(page, QStringLiteral("lineEndYSpinBox"));
    layout->addRow(tr("Start X"), m_LineStartXSpinBox);
    layout->addRow(tr("Start Y"), m_LineStartYSpinBox);
    layout->addRow(tr("End X"), m_LineEndXSpinBox);
    layout->addRow(tr("End Y"), m_LineEndYSpinBox);
    return page;
}

QWidget *GeometryCreateDialog::CreateArcPage()
{
    auto *page = new QWidget(this);
    auto *layout = new QFormLayout(page);
    m_ArcCenterXSpinBox = CreateSpinBox(page, QStringLiteral("arcCenterXSpinBox"));
    m_ArcCenterYSpinBox = CreateSpinBox(page, QStringLiteral("arcCenterYSpinBox"));
    m_ArcRadiusSpinBox = CreateSpinBox(page, QStringLiteral("arcRadiusSpinBox"));
    m_ArcStartAngleSpinBox = CreateSpinBox(page,
                                            QStringLiteral("arcStartAngleSpinBox"));
    m_ArcEndAngleSpinBox = CreateSpinBox(page,
                                          QStringLiteral("arcEndAngleSpinBox"));
    m_ArcReversedCheckBox = new QCheckBox(tr("Clockwise"), page);
    m_ArcReversedCheckBox->setObjectName(QStringLiteral("arcReversedCheckBox"));
    layout->addRow(tr("Center X"), m_ArcCenterXSpinBox);
    layout->addRow(tr("Center Y"), m_ArcCenterYSpinBox);
    layout->addRow(tr("Radius"), m_ArcRadiusSpinBox);
    layout->addRow(tr("Start angle (rad)"), m_ArcStartAngleSpinBox);
    layout->addRow(tr("End angle (rad)"), m_ArcEndAngleSpinBox);
    layout->addRow(QString(), m_ArcReversedCheckBox);
    return page;
}

QWidget *GeometryCreateDialog::CreateCirclePage()
{
    auto *page = new QWidget(this);
    auto *layout = new QFormLayout(page);
    m_CircleCenterXSpinBox = CreateSpinBox(page,
                                           QStringLiteral("circleCenterXSpinBox"));
    m_CircleCenterYSpinBox = CreateSpinBox(page,
                                           QStringLiteral("circleCenterYSpinBox"));
    m_CircleRadiusSpinBox = CreateSpinBox(page,
                                          QStringLiteral("circleRadiusSpinBox"));
    layout->addRow(tr("Center X"), m_CircleCenterXSpinBox);
    layout->addRow(tr("Center Y"), m_CircleCenterYSpinBox);
    layout->addRow(tr("Radius"), m_CircleRadiusSpinBox);
    return page;
}

QWidget *GeometryCreateDialog::CreatePolylinePage()
{
    auto *page = new QWidget(this);
    auto *layout = new QFormLayout(page);
    m_PolylineVerticesTextEdit = new QPlainTextEdit(page);
    m_PolylineVerticesTextEdit->setObjectName(
                QStringLiteral("polylineVerticesTextEdit"));
    m_PolylineVerticesTextEdit->setPlainText(
                QStringLiteral("0,0\n10,0\n10,10"));
    m_PolylineClosedCheckBox = new QCheckBox(tr("Closed"), page);
    m_PolylineClosedCheckBox->setObjectName(
                QStringLiteral("polylineClosedCheckBox"));
    layout->addRow(tr("Vertices (x,y per line)"), m_PolylineVerticesTextEdit);
    layout->addRow(QString(), m_PolylineClosedCheckBox);
    return page;
}

void GeometryCreateDialog::OnCreateRequested()
{
    if (CreateGeometry())
    {
        accept();
    }
}

void GeometryCreateDialog::OnGeometryTypeChanged(int index)
{
    if (m_ParameterStack && index >= 0 && index < m_ParameterStack->count())
    {
        m_ParameterStack->setCurrentIndex(index);
    }
}

bool GeometryCreateDialog::CreateGeometry()
{
    if (!m_Document)
    {
        QMessageBox::warning(this,
                             tr("Create geometry"),
                             tr("No canvas document is available."));
        return false;
    }

    const mini_graph::CanvasEntityType type =
            static_cast<mini_graph::CanvasEntityType>(m_TypeComboBox->currentData().toInt());
    mini_graph::ItemId itemId = 0;
    switch (type)
    {
    case mini_graph::CanvasEntityType::Point:
        itemId = m_Document->AddPoint(mini_graph::CanvasVector(
                                          m_PointXSpinBox->value(),
                                          m_PointYSpinBox->value()));
        break;
    case mini_graph::CanvasEntityType::Line:
        itemId = m_Document->AddLine(
                    mini_graph::CanvasVector(m_LineStartXSpinBox->value(),
                                             m_LineStartYSpinBox->value()),
                    mini_graph::CanvasVector(m_LineEndXSpinBox->value(),
                                             m_LineEndYSpinBox->value()));
        break;
    case mini_graph::CanvasEntityType::Arc:
        itemId = m_Document->AddArc(
                    mini_graph::CanvasVector(m_ArcCenterXSpinBox->value(),
                                             m_ArcCenterYSpinBox->value()),
                    m_ArcRadiusSpinBox->value(),
                    m_ArcStartAngleSpinBox->value(),
                    m_ArcEndAngleSpinBox->value(),
                    m_ArcReversedCheckBox->isChecked());
        break;
    case mini_graph::CanvasEntityType::Circle:
        itemId = m_Document->AddCircle(
                    mini_graph::CanvasVector(m_CircleCenterXSpinBox->value(),
                                             m_CircleCenterYSpinBox->value()),
                    m_CircleRadiusSpinBox->value());
        break;
    case mini_graph::CanvasEntityType::Polyline:
    {
        QVector<mini_graph::CanvasVector> vertices;
        if (basic_example::ParsePolylineText(
                    m_PolylineVerticesTextEdit->toPlainText(),
                    &vertices))
        {
            itemId = m_Document->AddPolyline(vertices,
                                             m_PolylineClosedCheckBox->isChecked());
        }
        break;
    }
    case mini_graph::CanvasEntityType::Invalid:
        break;
    }

    if (itemId == 0)
    {
        QMessageBox::warning(this,
                             tr("Create geometry"),
                             tr("The geometry parameters are invalid."));
        return false;
    }

    return true;
}
