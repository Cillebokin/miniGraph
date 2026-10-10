#include <QApplication>
#include <QColor>
#include <QElapsedTimer>
#include <QHash>
#include <QImage>
#include <QLocale>
#include <QMouseEvent>
#include <QPainter>
#include <QSysInfo>
#include <QTextStream>
#include <QTemporaryDir>
#include <QThread>
#include <QVector>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>

#include "MiniGraph/CanvasData.h"
#include "MiniGraph/CanvasDocument.h"
#include "MiniGraph/CanvasEntities.h"
#include "MiniGraph/CanvasGeometry.h"
#include "MiniGraph/CanvasSnap.h"
#include "MiniGraph/CanvasView.h"

namespace {

using namespace mini_graph;

const QSize kViewSize(1280, 720);
const int kWarmupSamples = 3;
const int kMeasuredSamples = 31;
const CanvasVector kInteractionProbeCenter(100000.0, -100000.0);

enum class SceneMix {
    Simple,
    CurveHeavy
};

struct BenchmarkResult {
    QString scenario;
    int entityCount;
    int curvePercent;
    QString workload;
    int iterationsPerSample;
    qreal medianMilliseconds;
    qreal p95Milliseconds;
};

int IterationsForEntityCount(int entityCount);
int CurvePercentage(SceneMix mix);
void ConfigureView(CanvasView *view,
                   CanvasDocument *document,
                   bool drawCanvasDecorations);
void SendClick(CanvasView *view, const QPointF &position);

qreal Percentile(const QVector<qreal> &sortedSamples, qreal percentile)
{
    if (sortedSamples.isEmpty()) {
        return 0.0;
    }

    const qreal position = (sortedSamples.size() - 1) * percentile;
    const int lowerIndex = static_cast<int>(std::floor(position));
    const int upperIndex = static_cast<int>(std::ceil(position));
    const qreal fraction = position - lowerIndex;
    return sortedSamples.at(lowerIndex)
            + (sortedSamples.at(upperIndex) - sortedSamples.at(lowerIndex))
            * fraction;
}

template <typename Operation>
BenchmarkResult Measure(const QString &scenario,
                        int entityCount,
                        int curvePercent,
                        const QString &workload,
                        int iterationsPerSample,
                        const Operation &operation)
{
    for (int sample = 0; sample < kWarmupSamples; ++sample) {
        for (int iteration = 0; iteration < iterationsPerSample; ++iteration) {
            operation();
        }
    }

    QVector<qreal> samples;
    samples.reserve(kMeasuredSamples);
    for (int sample = 0; sample < kMeasuredSamples; ++sample) {
        QElapsedTimer timer;
        timer.start();
        for (int iteration = 0; iteration < iterationsPerSample; ++iteration) {
            operation();
        }
        const qreal elapsedMilliseconds = timer.nsecsElapsed() / 1000000.0;
        samples.append(elapsedMilliseconds / iterationsPerSample);
    }
    std::sort(samples.begin(), samples.end());

    BenchmarkResult result;
    result.scenario = scenario;
    result.entityCount = entityCount;
    result.curvePercent = curvePercent;
    result.workload = workload;
    result.iterationsPerSample = iterationsPerSample;
    result.medianMilliseconds = Percentile(samples, 0.50);
    result.p95Milliseconds = Percentile(samples, 0.95);
    return result;
}

void PrintResult(QTextStream &output, const BenchmarkResult &result)
{
    output << result.scenario << ','
           << result.entityCount << ','
           << result.curvePercent << ','
           << result.workload << ','
           << QString::number(result.medianMilliseconds, 'f', 6) << ','
           << QString::number(result.p95Milliseconds, 'f', 6) << ','
           << result.iterationsPerSample << '\n';
}

#ifdef MINIGRAPH_ENABLE_PERF_PROFILING
QString CanvasEntityTypeName(CanvasEntityType type)
{
    switch (type) {
    case CanvasEntityType::Invalid: return QStringLiteral("Invalid");
    case CanvasEntityType::Point: return QStringLiteral("Point");
    case CanvasEntityType::Line: return QStringLiteral("Line");
    case CanvasEntityType::Arc: return QStringLiteral("Arc");
    case CanvasEntityType::Circle: return QStringLiteral("Circle");
    case CanvasEntityType::Polyline: return QStringLiteral("Polyline");
    case CanvasEntityType::Ellipse: return QStringLiteral("Ellipse");
    case CanvasEntityType::Spline: return QStringLiteral("Spline");
    case CanvasEntityType::GroupContainer:
        return QStringLiteral("GroupContainer");
    }
    return QStringLiteral("Unknown");
}

void PrintProfileMetric(QTextStream &output,
                        int entityCount,
                        int curvePercent,
                        const QString &workload,
                        const QString &metric,
                        QVector<qreal> samples,
                        const QString &unit)
{
    std::sort(samples.begin(), samples.end());
    output << "# MG010_PROFILE," << entityCount << ',' << curvePercent << ','
           << workload << ',' << metric << ','
           << QString::number(Percentile(samples, 0.50), 'f', 6) << ','
           << QString::number(Percentile(samples, 0.95), 'f', 6) << ','
           << unit << '\n';
}

bool MeasureClickProfile(QTextStream &output,
                         CanvasDocument *document,
                         int entityCount,
                         SceneMix mix,
                         bool hit)
{
    if (!document) {
        return false;
    }

    CanvasView view;
    ConfigureView(&view, document, false);
    if (hit) {
        view.CenterOnWorld(kInteractionProbeCenter);
    }
    const QPointF query = hit
            ? view.MapFromWorld(kInteractionProbeCenter)
            : QPointF(8.0, 8.0);
    SendClick(&view, query);
    if ((hit && !document->IsItemSelected(2))
            || (!hit && document->GetSelectedItemCount() != 0)) {
        output << "ERROR,profile_click_precondition_failed\n";
        return false;
    }
    document->ClearSelection();

    const int iterations = IterationsForEntityCount(entityCount);
    output << "# MG010_PROFILE_BEGIN," << entityCount << ','
           << CurvePercentage(mix) << ','
           << (hit ? "click_hit" : "click_miss") << '\n';
    output << "# MG010_PROFILE_SAMPLING,period=36,sampled_entities=6,"
              "stage_time_weight=6\n";
    output.flush();
    for (int sample = 0; sample < kWarmupSamples; ++sample) {
        view.ResetHitTestProfile();
        for (int iteration = 0; iteration < iterations; ++iteration) {
            SendClick(&view, query);
        }
    }

    QVector<qreal> eventMilliseconds;
    QVector<qreal> callsPerClick;
    QVector<qreal> itemEnumerationMilliseconds;
    QVector<qreal> entityLookupMilliseconds;
    QVector<qreal> pathCollectionMilliseconds;
    QVector<qreal> pathDistanceMilliseconds;
    QVector<qreal> hitTestTotalMilliseconds;
    QVector<qreal> pathCollectionByTypeMilliseconds[
            CanvasView::CanvasHitTestProfile::EntityTypeCount];
    QVector<qreal> pathDistanceByTypeMilliseconds[
            CanvasView::CanvasHitTestProfile::EntityTypeCount];
    quint64 entityCountsByType[
            CanvasView::CanvasHitTestProfile::EntityTypeCount] = {};
    quint64 pathCountsByType[
            CanvasView::CanvasHitTestProfile::EntityTypeCount] = {};
    quint64 pathPointCountsByType[
            CanvasView::CanvasHitTestProfile::EntityTypeCount] = {};
    quint64 measuredHitTestCount = 0;

    for (int sample = 0; sample < kMeasuredSamples; ++sample) {
        view.ResetHitTestProfile();
        QElapsedTimer eventTimer;
        eventTimer.start();
        for (int iteration = 0; iteration < iterations; ++iteration) {
            SendClick(&view, query);
        }
        eventMilliseconds.append(eventTimer.nsecsElapsed()
                                 / 1000000.0 / iterations);

        const CanvasView::CanvasHitTestProfile profile =
                view.GetHitTestProfile();
        quint64 entitiesByType = 0;
        quint64 pathsByType = 0;
        quint64 pathPointsByType = 0;
        for (int typeIndex = 0;
             typeIndex < CanvasView::CanvasHitTestProfile::EntityTypeCount;
             ++typeIndex) {
            entitiesByType += profile.entityCountByType[typeIndex];
            pathsByType += profile.selectionPathCountByType[typeIndex];
            pathPointsByType +=
                    profile.selectionPathPointCountByType[typeIndex];
        }
        if (profile.hitTestCallCount == 0
                || profile.itemIdVisitCount
                        != static_cast<quint64>(entityCount)
                        * profile.hitTestCallCount
                || entitiesByType != profile.itemIdVisitCount
                || pathsByType != profile.distancePathCount
                || pathPointsByType < pathsByType
                || profile.distancePathCount == 0
                || profile.pathCollectionNanoseconds == 0
                || profile.pathDistanceNanoseconds == 0
                || profile.totalNanoseconds == 0) {
            output << "ERROR,invalid_hit_test_profile," << entityCount << ','
                   << profile.hitTestCallCount << ','
                   << profile.itemIdVisitCount << ','
                   << profile.distancePathCount << '\n';
            return false;
        }

        const qreal callCount = static_cast<qreal>(profile.hitTestCallCount);
        callsPerClick.append(callCount / iterations);
        itemEnumerationMilliseconds.append(
                    profile.itemIdEnumerationNanoseconds
                    / 1000000.0 / callCount);
        entityLookupMilliseconds.append(
                    profile.entityLookupNanoseconds / 1000000.0 / callCount);
        pathCollectionMilliseconds.append(
                    profile.pathCollectionNanoseconds / 1000000.0 / callCount);
        pathDistanceMilliseconds.append(
                    profile.pathDistanceNanoseconds / 1000000.0 / callCount);
        hitTestTotalMilliseconds.append(
                    profile.totalNanoseconds / 1000000.0 / callCount);

        for (int typeIndex = 0;
             typeIndex < CanvasView::CanvasHitTestProfile::EntityTypeCount;
             ++typeIndex) {
            pathCollectionByTypeMilliseconds[typeIndex].append(
                        profile.pathCollectionNanosecondsByType[typeIndex]
                        / 1000000.0 / callCount);
            pathDistanceByTypeMilliseconds[typeIndex].append(
                        profile.pathDistanceNanosecondsByType[typeIndex]
                        / 1000000.0 / callCount);
            entityCountsByType[typeIndex] +=
                    profile.entityCountByType[typeIndex];
            pathCountsByType[typeIndex] +=
                    profile.selectionPathCountByType[typeIndex];
            pathPointCountsByType[typeIndex] +=
                    profile.selectionPathPointCountByType[typeIndex];
        }
        measuredHitTestCount += profile.hitTestCallCount;
    }

    const QString workload = hit ? QStringLiteral("click_hit")
                                 : QStringLiteral("click_miss");
    const int curvePercent = CurvePercentage(mix);
    PrintProfileMetric(output, entityCount, curvePercent, workload,
                       QStringLiteral("profiled_event_ms_per_click"),
                       eventMilliseconds, QStringLiteral("ms/click"));
    PrintProfileMetric(output, entityCount, curvePercent, workload,
                       QStringLiteral("hit_test_calls_per_click"),
                       callsPerClick, QStringLiteral("calls/click"));
    PrintProfileMetric(output, entityCount, curvePercent, workload,
                       QStringLiteral("item_id_enumeration"),
                       itemEnumerationMilliseconds,
                       QStringLiteral("ms/hit_test"));
    PrintProfileMetric(output, entityCount, curvePercent, workload,
                       QStringLiteral("entity_lookup"),
                       entityLookupMilliseconds,
                       QStringLiteral("ms/hit_test"));
    PrintProfileMetric(output, entityCount, curvePercent, workload,
                       QStringLiteral("selection_path_collection"),
                       pathCollectionMilliseconds,
                       QStringLiteral("ms/hit_test"));
    PrintProfileMetric(output, entityCount, curvePercent, workload,
                       QStringLiteral("path_distance_evaluation"),
                       pathDistanceMilliseconds,
                       QStringLiteral("ms/hit_test"));
    PrintProfileMetric(output, entityCount, curvePercent, workload,
                       QStringLiteral("hit_test_internal_total"),
                       hitTestTotalMilliseconds,
                       QStringLiteral("ms/hit_test"));

    for (int typeIndex = 0;
         typeIndex < CanvasView::CanvasHitTestProfile::EntityTypeCount;
         ++typeIndex) {
        QVector<qreal> sortedTypeSamples =
                pathCollectionByTypeMilliseconds[typeIndex];
        std::sort(sortedTypeSamples.begin(), sortedTypeSamples.end());
        QVector<qreal> sortedDistanceSamples =
                pathDistanceByTypeMilliseconds[typeIndex];
        std::sort(sortedDistanceSamples.begin(), sortedDistanceSamples.end());
        output << "# MG010_PROFILE_TYPE," << entityCount << ','
               << curvePercent << ',' << workload << ','
               << CanvasEntityTypeName(static_cast<CanvasEntityType>(typeIndex))
               << ",entities_per_hit_test="
               << QString::number(static_cast<qreal>(entityCountsByType[typeIndex])
                                  / measuredHitTestCount, 'f', 3)
               << ",paths_per_hit_test="
               << QString::number(static_cast<qreal>(pathCountsByType[typeIndex])
                                  / measuredHitTestCount, 'f', 3)
               << ",path_points_per_hit_test="
               << QString::number(static_cast<qreal>(
                                      pathPointCountsByType[typeIndex])
                                  / measuredHitTestCount, 'f', 3)
               << ",path_build_ms_per_hit_test_median="
               << QString::number(Percentile(sortedTypeSamples, 0.50),
                                  'f', 6)
               << ",path_build_ms_per_hit_test_p95="
               << QString::number(Percentile(sortedTypeSamples, 0.95),
                                  'f', 6)
               << ",path_distance_ms_per_hit_test_median="
               << QString::number(Percentile(sortedDistanceSamples, 0.50),
                                  'f', 6)
               << ",path_distance_ms_per_hit_test_p95="
               << QString::number(Percentile(sortedDistanceSamples, 0.95),
                                  'f', 6)
               << '\n';
    }
    output.flush();
    return true;
}
#endif

int IterationsForEntityCount(int entityCount)
{
    return qMax(1, 10000 / qMax(1, entityCount));
}

int CurvePercentage(SceneMix mix)
{
    return mix == SceneMix::CurveHeavy ? 67 : 0;
}

CanvasVector EntityPosition(int index, bool overlapAtProbe = false)
{
    if (overlapAtProbe) {
        return kInteractionProbeCenter;
    }

    const int column = index % 80;
    const int row = index / 80;
    return CanvasVector((column - 40) * 8.0, (row - 31) * 8.0);
}

bool AddBenchmarkEntity(CanvasDocument *document,
                        int index,
                        SceneMix mix,
                        bool overlapAtProbe = false)
{
    if (!document) {
        return false;
    }

    const CanvasVector position = EntityPosition(index, overlapAtProbe);
    if (mix == SceneMix::Simple) {
        if (index % 2 == 0) {
            return document->AddEntity(CanvasPoint(position)) != 0;
        }
        return document->AddEntity(CanvasLine(
                    position, CanvasVector(position.GetX() + 4.0,
                                           position.GetY()))) != 0;
    }

    switch (index % 6) {
    case 0:
        return document->AddEntity(CanvasPoint(position)) != 0;
    case 1:
        return document->AddEntity(CanvasLine(
                    position, CanvasVector(position.GetX() + 4.0,
                                           position.GetY()))) != 0;
    case 2:
        return document->AddEntity(CanvasArc(position, 2.0, 0.0,
                                              std::acos(-1.0) / 2.0)) != 0;
    case 3:
        return document->AddEntity(CanvasCircle(position, 2.0)) != 0;
    case 4: {
        CanvasEllipse ellipse;
        return ellipse.SetGeometry(position, CanvasVector(3.0, 0.0), 0.5,
                                   false)
                && document->AddEntity(ellipse) != 0;
    }
    default: {
        CanvasSpline spline;
        const QVector<CanvasVector> controlPoints = QVector<CanvasVector>()
                << CanvasVector(position.GetX() - 2.0, position.GetY())
                << CanvasVector(position.GetX(), position.GetY() + 3.0)
                << CanvasVector(position.GetX() + 2.0, position.GetY());
        const QVector<qreal> knots = QVector<qreal>()
                << 0.0 << 0.0 << 0.0 << 1.0 << 1.0 << 1.0;
        return spline.SetData(2, controlPoints, knots, QVector<qreal>(),
                              false, false)
                && document->AddEntity(spline) != 0;
    }
    }
}

bool PopulateDocument(CanvasDocument *document,
                      int entityCount,
                      SceneMix mix,
                      bool overlapAtProbe = false)
{
    if (!document || entityCount < 0) {
        return false;
    }

    for (int index = 0; index < entityCount; ++index) {
        if (!AddBenchmarkEntity(document, index, mix, overlapAtProbe)) {
            return false;
        }
    }
    return document->GetItemCount() == entityCount;
}

bool PrepareInteractionProbe(CanvasDocument *document)
{
    StruLineGeometry geometry;
    geometry.startPoint = CanvasVector(kInteractionProbeCenter.GetX() - 16.0,
                                       kInteractionProbeCenter.GetY());
    geometry.endPoint = CanvasVector(kInteractionProbeCenter.GetX() + 16.0,
                                     kInteractionProbeCenter.GetY());
    return document && document->SetLineGeometry(2, geometry);
}

void ConfigureView(CanvasView *view,
                   CanvasDocument *document,
                   bool drawCanvasDecorations)
{
    view->resize(kViewSize);
    view->SetGridVisible(drawCanvasDecorations);
    view->SetAxisVisible(drawCanvasDecorations);
    view->SetRulerVisible(drawCanvasDecorations);
    view->SetSnapEnabled(false);
    view->SetDocument(document);
    view->show();
    QApplication::processEvents();
}

void RenderView(CanvasView *view,
                QImage *target,
                QPainter *painter)
{
    Q_UNUSED(target)
    view->render(painter);
}

void SendMouseEvent(CanvasView *view,
                    QEvent::Type type,
                    const QPointF &position,
                    Qt::MouseButton button,
                    Qt::MouseButtons buttons)
{
    QMouseEvent event(type, position, button, buttons, Qt::NoModifier);
    QApplication::sendEvent(view, &event);
}

void SendClick(CanvasView *view, const QPointF &position)
{
    SendMouseEvent(view, QEvent::MouseButtonPress, position,
                   Qt::LeftButton, Qt::LeftButton);
    SendMouseEvent(view, QEvent::MouseButtonRelease, position,
                   Qt::LeftButton, Qt::NoButton);
}

#ifdef MINIGRAPH_ENABLE_PERF_PROFILING
struct BoundsParityTarget
{
    BoundsParityTarget()
        : itemId(0)
        , expectedHit(true)
        , zoom(1.0)
    {
    }

    BoundsParityTarget(const QString &targetName,
                       ItemId targetItemId,
                       const CanvasVector &targetQuery,
                       bool targetExpectedHit = true,
                       qreal targetZoom = 1.0)
        : name(targetName)
        , itemId(targetItemId)
        , query(targetQuery)
        , expectedHit(targetExpectedHit)
        , zoom(targetZoom)
    {
    }

    QString name;
    ItemId itemId;
    CanvasVector query;
    bool expectedHit;
    qreal zoom;
};

bool SetWideHitStyle(CanvasEntity *entity, bool point)
{
    if (!entity) {
        return false;
    }

    StruCanvasStyle style = entity->GetStyle();
    style.strokeWidth = 40.0;
    style.pointSize = point ? 40.0 : style.pointSize;
    return entity->SetStyle(style);
}

bool AddHitToleranceBoundsTargets(CanvasDocument *document,
                                  QVector<BoundsParityTarget> *targets,
                                  const QString &name,
                                  const CanvasEntity &entity,
                                  const CanvasVector &boundaryPoint,
                                  const CanvasVector &unitDirection)
{
    if (!document || !targets) {
        return false;
    }

    const ItemId id = document->AddEntity(entity);
    if (id == 0) {
        return false;
    }

    targets->append(BoundsParityTarget(
                        name + QStringLiteral("_within_tolerance"), id,
                        boundaryPoint + unitDirection * 5.5,
                        true, 1.0));
    targets->append(BoundsParityTarget(
                        name + QStringLiteral("_outside_tolerance"), id,
                        boundaryPoint + unitDirection * 6.5,
                        false, 1.0));
    return true;
}

void AppendExpectedMissTarget(QVector<BoundsParityTarget> *targets,
                              const QString &name,
                              ItemId id,
                              const CanvasVector &query)
{
    if (targets && id != 0) {
        targets->append(BoundsParityTarget(name, id, query, false, 1.0));
    }
}

void SendZoomSteps(CanvasView *view, int steps)
{
    if (!view || steps == 0) {
        return;
    }

    const QPointF anchor(view->width() / 2.0, view->height() / 2.0);
    QWheelEvent event(anchor, anchor, QPoint(0, 0), QPoint(0, steps * 120),
                      Qt::NoButton, Qt::NoModifier, Qt::ScrollUpdate, false);
    QApplication::sendEvent(view, &event);
}

bool SetBoundsValidationZoom(CanvasView *view, qreal zoom)
{
    if (!view || !std::isfinite(static_cast<double>(zoom)) || zoom <= 0.0) {
        return false;
    }

    view->ResetView();
    if (zoom > 1.0) {
        SendZoomSteps(view, 200);
    } else if (zoom < 1.0) {
        SendZoomSteps(view, -200);
    }

    return std::abs(view->GetZoom() - zoom) <= zoom * 1.0e-9;
}

bool BuildBoundsParityScene(CanvasDocument *document,
                            QVector<BoundsParityTarget> *targets)
{
    if (!document || !targets) {
        return false;
    }

    CanvasPoint point(CanvasVector(0.0, 0.0));
    if (!SetWideHitStyle(&point, true)) {
        return false;
    }
    ItemId id = document->AddEntity(point);
    if (id == 0) return false;
    targets->append(BoundsParityTarget(QStringLiteral("wide_Point"), id,
                                       CanvasVector(15.0, 0.0)));

    CanvasLine line(CanvasVector(1000.0, 0.0), CanvasVector(1100.0, 0.0));
    if (!SetWideHitStyle(&line, false)) return false;
    id = document->AddEntity(line);
    if (id == 0) return false;
    targets->append(BoundsParityTarget(QStringLiteral("wide_Line"), id,
                                       CanvasVector(1050.0, 15.0)));

    const qreal pi = std::acos(-1.0);
    CanvasArc arc(CanvasVector(2000.0, 0.0), 50.0, 0.0, pi / 2.0);
    if (!SetWideHitStyle(&arc, false)) return false;
    id = document->AddEntity(arc);
    if (id == 0) return false;
    targets->append(BoundsParityTarget(
                        QStringLiteral("wide_Arc"), id,
                        CanvasVector(2000.0 + 65.0 / std::sqrt(2.0),
                                     65.0 / std::sqrt(2.0))));

    CanvasCircle circle(CanvasVector(3000.0, 0.0), 50.0);
    if (!SetWideHitStyle(&circle, false)) return false;
    id = document->AddEntity(circle);
    if (id == 0) return false;
    targets->append(BoundsParityTarget(QStringLiteral("wide_Circle"), id,
                                       CanvasVector(3065.0, 0.0)));

    CanvasEllipse ellipse;
    if (!ellipse.SetGeometry(CanvasVector(4000.0, 0.0),
                             CanvasVector(50.0, 0.0), 0.5, false)
            || !SetWideHitStyle(&ellipse, false)) {
        return false;
    }
    id = document->AddEntity(ellipse);
    if (id == 0) return false;
    targets->append(BoundsParityTarget(QStringLiteral("wide_Ellipse"), id,
                                       CanvasVector(4065.0, 0.0)));

    CanvasPolyline polyline;
    CanvasLine polylineSegment(CanvasVector(5000.0, 0.0),
                               CanvasVector(5100.0, 0.0));
    if (!polyline.AddSegment(polylineSegment)
            || !SetWideHitStyle(&polyline, false)) {
        return false;
    }
    id = document->AddEntity(polyline);
    if (id == 0) return false;
    targets->append(BoundsParityTarget(QStringLiteral("wide_Polyline"), id,
                                       CanvasVector(5050.0, 15.0)));

    CanvasSpline spline;
    const QVector<CanvasVector> controlPoints = QVector<CanvasVector>()
            << CanvasVector(6000.0, 0.0)
            << CanvasVector(6050.0, 0.0)
            << CanvasVector(6100.0, 0.0);
    const QVector<qreal> knots = QVector<qreal>()
            << 0.0 << 0.0 << 0.0 << 1.0 << 1.0 << 1.0;
    if (!spline.SetData(2, controlPoints, knots, QVector<qreal>(),
                        false, false)
            || !SetWideHitStyle(&spline, false)) {
        return false;
    }
    id = document->AddEntity(spline);
    if (id == 0) return false;
    targets->append(BoundsParityTarget(QStringLiteral("wide_Spline"), id,
                                       CanvasVector(6050.0, 15.0)));

    CanvasGroupContainer group;
    CanvasLine groupLine(CanvasVector(7000.0, 0.0),
                         CanvasVector(7100.0, 0.0));
    if (!SetWideHitStyle(&groupLine, false)
            || !group.AddChild(groupLine)) {
        return false;
    }
    id = document->AddEntity(group);
    if (id == 0) return false;
    targets->append(BoundsParityTarget(
                        QStringLiteral("wide_GroupContainer"), id,
                        CanvasVector(7050.0, 15.0)));

    const CanvasVector up(0.0, 1.0);
    const CanvasVector right(1.0, 0.0);

    const CanvasVector pointPosition(10000.0, 0.0);
    if (!AddHitToleranceBoundsTargets(
                document, targets, QStringLiteral("point_tolerance"),
                CanvasPoint(pointPosition), pointPosition, right)) {
        return false;
    }

    const CanvasVector lineStart(11000.0, 0.0);
    const CanvasLine boundaryLine(lineStart,
                                  CanvasVector(11100.0, 0.0));
    if (!AddHitToleranceBoundsTargets(
                document, targets, QStringLiteral("line_tolerance"),
                boundaryLine, CanvasVector(11050.0, 0.0), up)) {
        return false;
    }

    const CanvasVector arcCenter(12000.0, 0.0);
    const qreal arcAngle = 0.7;
    const CanvasVector arcDirection(std::cos(arcAngle), std::sin(arcAngle));
    const CanvasVector arcBoundary = arcCenter + arcDirection * 50.0;
    const CanvasArc boundaryArc(arcCenter, 50.0, 0.0, pi / 2.0);
    if (!AddHitToleranceBoundsTargets(
                document, targets, QStringLiteral("arc_tolerance"),
                boundaryArc, arcBoundary, arcDirection)) {
        return false;
    }

    const CanvasVector reversedArcCenter(12500.0, 0.0);
    const CanvasVector reversedArcBoundary = reversedArcCenter
            + arcDirection * 50.0;
    const CanvasArc reversedArc(reversedArcCenter, 50.0, pi / 2.0, 0.0,
                                true);
    if (!AddHitToleranceBoundsTargets(
                document, targets, QStringLiteral("arc_reversed_tolerance"),
                reversedArc, reversedArcBoundary, arcDirection)) {
        return false;
    }

    const CanvasVector circleCenter(13000.0, 0.0);
    const CanvasCircle boundaryCircle(circleCenter, 50.0);
    if (!AddHitToleranceBoundsTargets(
                document, targets, QStringLiteral("circle_tolerance"),
                boundaryCircle, circleCenter + right * 50.0, right)) {
        return false;
    }

    const CanvasVector ellipseCenter(14000.0, 0.0);
    CanvasEllipse boundaryEllipse;
    if (!boundaryEllipse.SetGeometry(ellipseCenter,
                                     CanvasVector(50.0, 0.0), 0.5, false)
            || !AddHitToleranceBoundsTargets(
                document, targets, QStringLiteral("ellipse_tolerance"),
                boundaryEllipse, ellipseCenter + right * 50.0, right)) {
        return false;
    }

    const CanvasVector ellipseArcCenter(15000.0, 0.0);
    CanvasEllipse boundaryEllipseArc;
    if (!boundaryEllipseArc.SetGeometry(ellipseArcCenter,
                                        CanvasVector(50.0, 0.0), 0.5, true,
                                        0.0, pi / 2.0)
            || !AddHitToleranceBoundsTargets(
                document, targets, QStringLiteral("ellipse_arc_tolerance"),
                boundaryEllipseArc, ellipseArcCenter + right * 50.0, right)) {
        return false;
    }

    const CanvasVector polylineStart(16000.0, 0.0);
    CanvasPolyline boundaryPolyline;
    const CanvasLine visiblePolylineSegment(
                polylineStart, CanvasVector(16100.0, 0.0));
    CanvasLine hiddenPolylineSegment(CanvasVector(16100.0, 0.0),
                                     CanvasVector(16200.0, 0.0));
    StruCanvasStyle hiddenSegmentStyle = hiddenPolylineSegment.GetStyle();
    hiddenSegmentStyle.visible = false;
    if (!hiddenPolylineSegment.SetStyle(hiddenSegmentStyle)
            || !boundaryPolyline.AddSegment(visiblePolylineSegment)
            || !boundaryPolyline.AddSegment(hiddenPolylineSegment)) {
        return false;
    }
    id = document->AddEntity(boundaryPolyline);
    if (id == 0) {
        return false;
    }
    targets->append(BoundsParityTarget(
                        QStringLiteral("polyline_visible_within_tolerance"),
                        id, CanvasVector(16050.0, 5.5), true));
    targets->append(BoundsParityTarget(
                        QStringLiteral("polyline_visible_outside_tolerance"),
                        id, CanvasVector(16050.0, 6.5), false));
    AppendExpectedMissTarget(targets,
                             QStringLiteral("polyline_invisible_segment"),
                             id, CanvasVector(16150.0, 0.0));

    const CanvasVector splineStart(17000.0, 0.0);
    const QVector<CanvasVector> splineControlPoints = QVector<CanvasVector>()
            << splineStart << CanvasVector(17050.0, 0.0)
            << CanvasVector(17100.0, 0.0);
    const QVector<qreal> splineKnots = QVector<qreal>()
            << 0.0 << 0.0 << 0.0 << 1.0 << 1.0 << 1.0;
    CanvasSpline polynomialSpline;
    if (!polynomialSpline.SetData(2, splineControlPoints, splineKnots,
                                  QVector<qreal>(), false, false)
            || !AddHitToleranceBoundsTargets(
                document, targets, QStringLiteral("spline_polynomial_tolerance"),
                polynomialSpline, CanvasVector(17050.0, 0.0), up)) {
        return false;
    }

    const CanvasVector rationalSplineStart(18000.0, 0.0);
    const QVector<CanvasVector> rationalControlPoints = QVector<CanvasVector>()
            << rationalSplineStart << CanvasVector(18050.0, 0.0)
            << CanvasVector(18100.0, 0.0);
    const QVector<qreal> rationalWeights = QVector<qreal>()
            << 1.0 << 2.0 << 1.0;
    CanvasSpline rationalSpline;
    if (!rationalSpline.SetData(2, rationalControlPoints, splineKnots,
                                rationalWeights, false, false)
            || !AddHitToleranceBoundsTargets(
                document, targets, QStringLiteral("spline_rational_tolerance"),
                rationalSpline, CanvasVector(18050.0, 0.0), up)) {
        return false;
    }

    const CanvasVector groupStart(19000.0, 0.0);
    CanvasLine nestedLine(groupStart, CanvasVector(19100.0, 0.0));
    CanvasGroupContainer innerGroup;
    CanvasGroupContainer nestedGroup;
    if (!innerGroup.AddChild(nestedLine)
            || !nestedGroup.AddChild(innerGroup)
            || !AddHitToleranceBoundsTargets(
                document, targets, QStringLiteral("group_nested_tolerance"),
                nestedGroup, CanvasVector(19050.0, 0.0), up)) {
        return false;
    }

    const CanvasVector hiddenGroupStart(20000.0, 0.0);
    CanvasLine hiddenGroupLine(hiddenGroupStart,
                               CanvasVector(20100.0, 0.0));
    StruCanvasStyle hiddenGroupStyle = hiddenGroupLine.GetStyle();
    hiddenGroupStyle.visible = false;
    CanvasGroupContainer hiddenInnerGroup;
    CanvasGroupContainer hiddenOuterGroup;
    if (!hiddenGroupLine.SetStyle(hiddenGroupStyle)
            || !hiddenInnerGroup.AddChild(hiddenGroupLine)
            || !hiddenOuterGroup.AddChild(hiddenInnerGroup)) {
        return false;
    }
    id = document->AddEntity(hiddenOuterGroup);
    if (id == 0) {
        return false;
    }
    AppendExpectedMissTarget(targets,
                             QStringLiteral("group_nested_invisible_child"),
                             id, CanvasVector(20050.0, 0.0));

    const CanvasVector farPointPosition(1.0e12, -1.0e12);
    if (!AddHitToleranceBoundsTargets(
                document, targets, QStringLiteral("large_finite_coordinate"),
                CanvasPoint(farPointPosition), farPointPosition, right)) {
        return false;
    }

    const qreal minimumZoom = CanvasTransform::GetMinimumZoom();
    const qreal maximumZoom = CanvasTransform::GetMaximumZoom();
    const CanvasVector zoomPointPositions[] = {
        CanvasVector(-1.0e12, 1.0e12),
        CanvasVector(30000.0, 0.0)
    };
    const qreal testedZooms[] = {minimumZoom, maximumZoom};
    const char *zoomNames[] = {"minimum_zoom", "maximum_zoom"};
    for (int index = 0; index < 2; ++index) {
        const qreal zoom = testedZooms[index];
        const CanvasVector zoomPointPosition = zoomPointPositions[index];
        const ItemId zoomPointId = document->AddEntity(
                    CanvasPoint(zoomPointPosition));
        if (zoomPointId == 0) {
            return false;
        }
        const qreal withinOffset = 5.5 / zoom;
        const qreal outsideOffset = 6.5 / zoom;
        targets->append(BoundsParityTarget(
                            QString::fromLatin1(zoomNames[index])
                                + QStringLiteral("_within_tolerance"),
                            zoomPointId,
                            zoomPointPosition + right * withinOffset,
                            true, zoom));
        targets->append(BoundsParityTarget(
                            QString::fromLatin1(zoomNames[index])
                                + QStringLiteral("_outside_tolerance"),
                            zoomPointId,
                            zoomPointPosition + right * outsideOffset,
                            false, zoom));
    }

    return targets->size() >= 30;
}

bool VerifyBoundsFilterHitParity(QTextStream &output)
{
    CanvasDocument document;
    QVector<BoundsParityTarget> targets;
    if (!BuildBoundsParityScene(&document, &targets)) {
        output << "ERROR,bounds_parity_scene_build_failed\n";
        return false;
    }

    CanvasView view;
    ConfigureView(&view, &document, false);
    view.setUpdatesEnabled(false);
    for (const BoundsParityTarget &target : targets) {
        if (!SetBoundsValidationZoom(&view, target.zoom)) {
            output << "ERROR,bounds_parity_zoom_setup_failed,"
                   << target.name << ',' << target.zoom << '\n';
            return false;
        }
        view.CenterOnWorld(target.query);
        const QPointF viewportQuery = view.MapFromWorld(target.query);
        QVector<ItemId> expectedSelection;
        if (target.expectedHit) {
            expectedSelection.append(target.itemId);
        }
        QVector<ItemId> outcomes[2];
        for (int mode = 0; mode < 2; ++mode) {
            document.ClearSelection();
            view.SetHitTestBoundsFilterEnabledForProfiling(mode == 1);
            view.ResetHitTestProfile();
            SendClick(&view, viewportQuery);
            outcomes[mode] = document.GetSelectedItemIds();
            if (outcomes[mode] != expectedSelection) {
                output << "ERROR,bounds_parity_expected_result_failed,"
                       << target.name << ',' << mode
                       << ",expected_count=" << expectedSelection.size()
                       << ",actual_count=" << outcomes[mode].size()
                       << ",actual_item="
                       << (outcomes[mode].isEmpty()
                           ? 0 : outcomes[mode].first())
                       << ",query_world=" << target.query.GetX() << ':'
                       << target.query.GetY()
                       << ",mapped_query=" << viewportQuery.x() << ':'
                       << viewportQuery.y()
                       << ",zoom=" << view.GetZoom() << '\n';
                return false;
            }
        }
        if (outcomes[0] != outcomes[1]) {
            output << "ERROR,bounds_parity_selection_mismatch,"
                   << target.name << '\n';
            return false;
        }
        output << "# MG010_BOUNDS_PARITY," << target.name
               << ",expected=" << (target.expectedHit ? "hit" : "miss")
               << ",baseline_item="
               << (outcomes[0].isEmpty() ? 0 : outcomes[0].first())
               << ",filtered_item="
               << (outcomes[1].isEmpty() ? 0 : outcomes[1].first())
               << ",zoom=" << QString::number(target.zoom, 'g', 10) << '\n';
    }

    CanvasLine unrepresentableBoundsLine(
                CanvasVector(-std::numeric_limits<qreal>::max(), 0.0),
                CanvasVector(std::numeric_limits<qreal>::max(), 0.0));
    QRectF unrepresentableBounds;
    CanvasDocument rejectedBoundsDocument;
    if (!unrepresentableBoundsLine.IsValid()
            || TryGetGeometryBounds(unrepresentableBoundsLine,
                                    &unrepresentableBounds)
            || rejectedBoundsDocument.AddEntity(unrepresentableBoundsLine) != 0) {
        output << "ERROR,bounds_unrepresentable_entity_guard_failed\n";
        return false;
    }
    output << "# MG010_BOUNDS_INVALID,case=unrepresentable_line_bounds"
              ",geometry_bounds_available=false,document_accepts=false"
              ",hit_test_fallback_reachable=false\n";
    output << "# MG010_BOUNDS_PARITY_SUMMARY,cases=" << targets.size()
           << ",filter_off_on_selection_parity=true\n";
    return true;
}

struct BoundsModeSamples
{
    QVector<qreal> eventMilliseconds;
    QVector<qreal> pathCollectionMilliseconds;
    QVector<qreal> boundsCheckMilliseconds;
    quint64 pathCollectionCalls;
    quint64 itemIdVisits;
    quint64 boundsChecks;
    quint64 boundsRejects;
    quint64 hitTestCalls;
    quint64 selectionPathCount;
    quint64 selectionPathPointCount;
    quint64 distanceSegmentCount;

    BoundsModeSamples()
        : pathCollectionCalls(0)
        , itemIdVisits(0)
        , boundsChecks(0)
        , boundsRejects(0)
        , hitTestCalls(0)
        , selectionPathCount(0)
        , selectionPathPointCount(0)
        , distanceSegmentCount(0)
    {
    }
};

enum class BoundsFilterWorkload
{
    SparseHit,
    Miss,
    DenseHit,
    SelectivityHit,
    BoundsNearMiss
};

// Builds a fixed-size shape at a chosen center; query-centered shapes overlap
// the probe AABB while their selection path stays outside its hit tolerance.
bool AddBoundsOverlapFalsePositive(CanvasDocument *document,
                                   int candidateIndex,
                                   SceneMix mix,
                                   const CanvasVector &center)
{
    if (!document || candidateIndex < 0 || !center.IsValid()) {
        return false;
    }

    const auto addDiagonalLine = [document, center]() {
        const CanvasVector start(center.GetX() - 20.0,
                                 center.GetY() - 4.0);
        const CanvasVector end(center.GetX() + 4.0,
                               center.GetY() + 20.0);
        return document->AddEntity(CanvasLine(start, end)) != 0;
    };

    if (mix == SceneMix::Simple) {
        return addDiagonalLine();
    }

    switch (candidateIndex % 6) {
    case 0:
    case 1:
        return addDiagonalLine();
    case 2:
        return document->AddEntity(CanvasArc(
                    center, 10.0, 0.0, std::acos(-1.0) / 2.0)) != 0;
    case 3:
        return document->AddEntity(CanvasCircle(center, 10.0)) != 0;
    case 4: {
        CanvasEllipse ellipse;
        return ellipse.SetGeometry(center, CanvasVector(10.0, 0.0), 0.8,
                                   false)
                && document->AddEntity(ellipse) != 0;
    }
    default: {
        const QVector<CanvasVector> controlPoints = QVector<CanvasVector>()
                << CanvasVector(center.GetX() - 20.0, center.GetY() + 1.0)
                << CanvasVector(center.GetX(), center.GetY() - 50.0)
                << CanvasVector(center.GetX() + 20.0, center.GetY() + 1.0);
        const QVector<qreal> knots = QVector<qreal>()
                << 0.0 << 0.0 << 0.0 << 1.0 << 1.0 << 1.0;
        CanvasSpline spline;
        return spline.SetData(2, controlPoints, knots, QVector<qreal>(),
                              false, false)
                && document->AddEntity(spline) != 0;
    }
    }
}

bool PopulateBoundsSelectivityDocument(CanvasDocument *document,
                                       int entityCount,
                                       SceneMix mix,
                                       int candidatePercent,
                                       bool includeExactHit)
{
    if (!document || entityCount <= 0
            || candidatePercent <= 0 || candidatePercent > 100
            || (entityCount * candidatePercent) % 100 != 0) {
        return false;
    }

    const int candidateCount = entityCount * candidatePercent / 100;
    if (candidateCount <= 0 || candidateCount > entityCount) {
        return false;
    }

    const CanvasVector query = kInteractionProbeCenter;
    for (int index = 0; index < entityCount; ++index) {
        if (index < candidateCount && includeExactHit && index == 0) {
            const CanvasVector start(query.GetX() - 16.0, query.GetY());
            const CanvasVector end(query.GetX() + 16.0, query.GetY());
            if (document->AddEntity(CanvasLine(start, end)) == 0) {
                return false;
            }
        } else {
            const CanvasVector entityCenter = index < candidateCount
                    ? query : EntityPosition(index);
            if (!AddBoundsOverlapFalsePositive(document, index, mix,
                                               entityCenter)) {
                return false;
            }
        }
    }

    return document->GetItemCount() == entityCount;
}

bool SetBoundsBenchmarkDisplaySize(CanvasEntity *entity)
{
    if (!entity) {
        return false;
    }

    StruCanvasStyle style = entity->GetStyle();
    style.strokeWidth = 0.0;
    return entity->SetStyle(style);
}

bool ConfigureBoundsBenchmarkSpline(CanvasSpline *spline,
                                    const CanvasVector &center,
                                    bool exactHit)
{
    if (!spline || !center.IsValid()) {
        return false;
    }

    if (exactHit) {
        const QVector<CanvasVector> controlPoints = QVector<CanvasVector>()
                << CanvasVector(center.GetX() - 16.0, center.GetY())
                << CanvasVector(center.GetX() + 16.0, center.GetY());
        const QVector<qreal> knots = QVector<qreal>()
                << 0.0 << 0.0 << 1.0 << 1.0;
        return spline->SetData(1, controlPoints, knots, QVector<qreal>(),
                               false, false);
    }

    const QVector<CanvasVector> controlPoints = QVector<CanvasVector>()
            << CanvasVector(center.GetX() - 20.0, center.GetY() + 1.0)
            << CanvasVector(center.GetX(), center.GetY() - 50.0)
            << CanvasVector(center.GetX() + 20.0, center.GetY() + 1.0);
    const QVector<qreal> knots = QVector<qreal>()
            << 0.0 << 0.0 << 0.0 << 1.0 << 1.0 << 1.0;
    return spline->SetData(2, controlPoints, knots, QVector<qreal>(),
                           false, false);
}

bool AddBoundsBenchmarkPolyline(CanvasDocument *document,
                                const CanvasVector &center,
                                bool exactHit)
{
    if (!document || !center.IsValid()) {
        return false;
    }

    QVector<CanvasVector> vertices;
    if (exactHit) {
        vertices << CanvasVector(center.GetX() - 16.0, center.GetY())
                 << CanvasVector(center.GetX(), center.GetY())
                 << CanvasVector(center.GetX() + 16.0, center.GetY())
                 << CanvasVector(center.GetX() + 16.0, center.GetY() + 16.0);
    } else {
        vertices << CanvasVector(center.GetX() - 12.0, center.GetY() - 12.0)
                 << CanvasVector(center.GetX() + 12.0, center.GetY() - 12.0)
                 << CanvasVector(center.GetX() + 12.0, center.GetY() + 12.0)
                 << CanvasVector(center.GetX() - 12.0, center.GetY() + 12.0);
    }

    CanvasPolyline polyline;
    for (int index = 1; index < vertices.size(); ++index) {
        CanvasLine segment(vertices.at(index - 1), vertices.at(index));
        if (!SetBoundsBenchmarkDisplaySize(&segment)
                || !polyline.AddSegment(segment)) {
            return false;
        }
    }

    return SetBoundsBenchmarkDisplaySize(&polyline)
            && document->AddEntity(polyline) != 0;
}

bool AddBoundsBenchmarkGroup(CanvasDocument *document,
                             const CanvasVector &center,
                             bool exactHit)
{
    if (!document || !center.IsValid()) {
        return false;
    }

    const qreal pointOffset = 4.8;
    CanvasPoint point(exactHit
                      ? center
                      : CanvasVector(center.GetX() + pointOffset,
                                     center.GetY() + pointOffset));
    CanvasLine line;
    const bool lineGeometrySet = exactHit
            ? line.SetGeometry(
                CanvasVector(center.GetX() - 16.0, center.GetY()),
                CanvasVector(center.GetX() + 16.0, center.GetY()))
            : line.SetGeometry(
                CanvasVector(center.GetX() - 20.0, center.GetY() - 4.0),
                CanvasVector(center.GetX() + 4.0, center.GetY() + 20.0));
    CanvasSpline spline;
    CanvasGroupContainer group;
    if (!SetBoundsBenchmarkDisplaySize(&point)
            || !lineGeometrySet || !SetBoundsBenchmarkDisplaySize(&line)
            || !ConfigureBoundsBenchmarkSpline(&spline, center, false)
            || !SetBoundsBenchmarkDisplaySize(&spline)
            || !group.AddChild(point)
            || !group.AddChild(line)
            || !group.AddChild(spline)
            || !SetBoundsBenchmarkDisplaySize(&group)) {
        return false;
    }

    return document->AddEntity(group) != 0;
}

bool AddBoundsBenchmarkEntity(CanvasDocument *document,
                              CanvasEntityType type,
                              const CanvasVector &center,
                              bool exactHit,
                              QString *failureReason = nullptr)
{
    if (!document || !center.IsValid()) {
        return false;
    }

    const qreal pi = std::acos(-1.0);
    switch (type) {
    case CanvasEntityType::Point: {
        const qreal offset = 4.8;
        CanvasPoint point(exactHit
                          ? center
                          : CanvasVector(center.GetX() + offset,
                                         center.GetY() + offset));
        if (!SetBoundsBenchmarkDisplaySize(&point)) {
            if (failureReason) {
                *failureReason = QStringLiteral("point_style_rejected");
            }
            return false;
        }
        if (document->AddEntity(point) == 0) {
            if (failureReason) {
                *failureReason = QStringLiteral("point_document_add_rejected");
            }
            return false;
        }
        return true;
    }
    case CanvasEntityType::Line: {
        CanvasLine line;
        const bool geometrySet = exactHit
                ? line.SetGeometry(
                    CanvasVector(center.GetX() - 16.0, center.GetY()),
                    CanvasVector(center.GetX() + 16.0, center.GetY()))
                : line.SetGeometry(
                    CanvasVector(center.GetX() - 20.0, center.GetY() - 4.0),
                    CanvasVector(center.GetX() + 4.0, center.GetY() + 20.0));
        return geometrySet && SetBoundsBenchmarkDisplaySize(&line)
                && document->AddEntity(line) != 0;
    }
    case CanvasEntityType::Arc: {
        const CanvasVector arcCenter = exactHit
                ? CanvasVector(center.GetX() - 12.0, center.GetY())
                : center;
        CanvasArc arc(arcCenter, 12.0, 0.0, pi / 2.0);
        return SetBoundsBenchmarkDisplaySize(&arc)
                && document->AddEntity(arc) != 0;
    }
    case CanvasEntityType::Circle: {
        const CanvasVector circleCenter = exactHit
                ? CanvasVector(center.GetX() + 12.0, center.GetY())
                : center;
        CanvasCircle circle(circleCenter, 12.0);
        return SetBoundsBenchmarkDisplaySize(&circle)
                && document->AddEntity(circle) != 0;
    }
    case CanvasEntityType::Ellipse: {
        const CanvasVector ellipseCenter = exactHit
                ? CanvasVector(center.GetX() + 12.0, center.GetY())
                : center;
        CanvasEllipse ellipse;
        return ellipse.SetGeometry(ellipseCenter, CanvasVector(12.0, 0.0),
                                   0.75, false)
                && SetBoundsBenchmarkDisplaySize(&ellipse)
                && document->AddEntity(ellipse) != 0;
    }
    case CanvasEntityType::Polyline:
        return AddBoundsBenchmarkPolyline(document, center, exactHit);
    case CanvasEntityType::Spline: {
        CanvasSpline spline;
        return ConfigureBoundsBenchmarkSpline(&spline, center, exactHit)
                && SetBoundsBenchmarkDisplaySize(&spline)
                && document->AddEntity(spline) != 0;
    }
    case CanvasEntityType::GroupContainer:
        return AddBoundsBenchmarkGroup(document, center, exactHit);
    case CanvasEntityType::Invalid:
        return false;
    }

    return false;
}

bool PopulateBoundsEntityTypeDocument(CanvasDocument *document,
                                      CanvasEntityType type,
                                      int entityCount,
                                      int candidatePercent,
                                      QString *failureReason = nullptr)
{
    if (!document || type == CanvasEntityType::Invalid || entityCount <= 0
            || candidatePercent <= 0 || candidatePercent > 100
            || (entityCount * candidatePercent) % 100 != 0) {
        if (failureReason) {
            *failureReason = QStringLiteral("invalid_parameters");
        }
        return false;
    }

    const int candidateCount = entityCount * candidatePercent / 100;
    if (candidateCount <= 0 || candidateCount > entityCount) {
        if (failureReason) {
            *failureReason = QStringLiteral("invalid_candidate_count");
        }
        return false;
    }

    const CanvasVector query = kInteractionProbeCenter;
    for (int index = 0; index < entityCount; ++index) {
        const bool isCandidate = index < candidateCount;
        const CanvasVector center = isCandidate
                ? query : EntityPosition(index);
        QString entityFailureReason;
        if (!AddBoundsBenchmarkEntity(document, type, center, index == 0,
                                      &entityFailureReason)) {
            if (failureReason) {
                *failureReason = QStringLiteral("entity_add_failed_at_%1_%2")
                        .arg(index).arg(entityFailureReason);
            }
            return false;
        }
    }

    if (document->GetItemCount() != entityCount) {
        if (failureReason) {
            *failureReason = QStringLiteral("item_count_%1_expected_%2")
                    .arg(document->GetItemCount()).arg(entityCount);
        }
        return false;
    }
    const QVector<ItemId> ids = document->GetItemIds();
    for (const ItemId id : ids) {
        const CanvasEntity *entity = document->GetEntity(id);
        if (!entity || entity->GetType() != type) {
            if (failureReason) {
                *failureReason = QStringLiteral("entity_type_mismatch_for_id_%1")
                        .arg(static_cast<qulonglong>(id));
            }
            return false;
        }
    }
    return true;
}

int BoundsEntityCurvePercentage(CanvasEntityType type)
{
    switch (type) {
    case CanvasEntityType::Arc:
    case CanvasEntityType::Circle:
    case CanvasEntityType::Ellipse:
    case CanvasEntityType::Spline:
        return 100;
    case CanvasEntityType::GroupContainer:
        return 33;
    case CanvasEntityType::Invalid:
    case CanvasEntityType::Point:
    case CanvasEntityType::Line:
    case CanvasEntityType::Polyline:
        return 0;
    }
    return 0;
}

int BoundsEntitySelectionPathsPerEntity(CanvasEntityType type)
{
    return type == CanvasEntityType::Polyline
            || type == CanvasEntityType::GroupContainer ? 3 : 1;
}

QString BoundsFilterWorkloadName(BoundsFilterWorkload workload)
{
    switch (workload) {
    case BoundsFilterWorkload::SparseHit:
        return QStringLiteral("sparse_hit");
    case BoundsFilterWorkload::Miss:
        return QStringLiteral("miss");
    case BoundsFilterWorkload::DenseHit:
        return QStringLiteral("dense_overlap_hit");
    case BoundsFilterWorkload::SelectivityHit:
        return QStringLiteral("selectivity_hit");
    case BoundsFilterWorkload::BoundsNearMiss:
        return QStringLiteral("bounds_near_miss");
    }
    return QStringLiteral("unknown");
}

bool MeasureBoundsFilterAba(QTextStream &output,
                            CanvasDocument *document,
                            int entityCount,
                            SceneMix mix,
                            BoundsFilterWorkload workloadType,
                            int candidatePercent = -1,
                            CanvasEntityType entityType =
                                CanvasEntityType::Invalid,
                            ItemId expectedHitId = 0,
                            const QString &complexityLabel = QString(),
                            int complexitySelectionPathsPerEntity = -1)
{
    if (!document) return false;

    const bool entityTypeScoped = entityType != CanvasEntityType::Invalid;
    const int curvePercent = entityTypeScoped
            ? BoundsEntityCurvePercentage(entityType)
            : CurvePercentage(mix);
    const bool queryAtProbe = workloadType != BoundsFilterWorkload::Miss;
    const bool expectedHit = workloadType == BoundsFilterWorkload::SparseHit
            || workloadType == BoundsFilterWorkload::DenseHit
            || workloadType == BoundsFilterWorkload::SelectivityHit;
    int expectedBoundsCandidates = 0;
    if (workloadType == BoundsFilterWorkload::SparseHit) {
        expectedBoundsCandidates = 1;
    } else if (workloadType == BoundsFilterWorkload::DenseHit) {
        expectedBoundsCandidates = entityCount;
    } else if (workloadType == BoundsFilterWorkload::SelectivityHit
               || workloadType == BoundsFilterWorkload::BoundsNearMiss) {
        if (candidatePercent <= 0 || candidatePercent > 100
                || (entityCount * candidatePercent) % 100 != 0) {
            output << "ERROR,bounds_ab_invalid_candidate_percent,"
                   << entityCount << ',' << candidatePercent << '\n';
            return false;
        }
        expectedBoundsCandidates = entityCount * candidatePercent / 100;
    }
    if (expectedBoundsCandidates < 0
            || expectedBoundsCandidates > entityCount
            || (expectedHit && expectedBoundsCandidates == 0)) {
        output << "ERROR,bounds_ab_invalid_candidate_count," << entityCount
               << ',' << expectedBoundsCandidates << '\n';
        return false;
    }

    QString workload = BoundsFilterWorkloadName(workloadType);
    if (entityTypeScoped) {
        workload = QStringLiteral("entity_%1_%2")
                .arg(CanvasEntityTypeName(entityType).toLower(), workload);
    }
    if (!complexityLabel.isEmpty()) {
        workload = QStringLiteral("%1_%2").arg(complexityLabel, workload);
    }
    if (candidatePercent > 0) {
        workload += QStringLiteral("_%1pct").arg(candidatePercent);
    }
    CanvasView view;
    ConfigureView(&view, document, false);
    view.setUpdatesEnabled(false);
    const CanvasVector worldQuery = queryAtProbe
            ? kInteractionProbeCenter
            : CanvasVector(1000000.0, 1000000.0);
    view.CenterOnWorld(worldQuery);
    const QPointF viewportQuery = view.MapFromWorld(worldQuery);

    view.SetHitTestBoundsFilterEnabledForProfiling(false);
    document->ClearSelection();
    SendClick(&view, viewportQuery);
    const QVector<ItemId> expectedSelection = document->GetSelectedItemIds();
    if ((expectedHit && expectedSelection.size() != 1)
            || (!expectedHit && !expectedSelection.isEmpty())
            || (expectedHitId != 0
                && (expectedSelection.size() != 1
                    || expectedSelection.first() != expectedHitId))) {
        output << "ERROR,bounds_ab_scene_precondition_failed,"
               << entityCount << ',' << workload << '\n';
        return false;
    }

    BoundsModeSamples modes[2];
    for (int sample = 0; sample < kWarmupSamples; ++sample) {
        const int firstMode = sample % 2;
        for (int order = 0; order < 2; ++order) {
            const int mode = order == 0 ? firstMode : 1 - firstMode;
            document->ClearSelection();
            view.SetHitTestBoundsFilterEnabledForProfiling(mode == 1);
            SendClick(&view, viewportQuery);
            if (document->GetSelectedItemIds() != expectedSelection) {
                output << "ERROR,bounds_ab_warmup_selection_mismatch,"
                       << entityCount << ',' << workload << ',' << mode
                       << '\n';
                return false;
            }
        }
    }

    for (int sample = 0; sample < kMeasuredSamples; ++sample) {
        const int firstMode = sample % 2;
        QVector<ItemId> pairedOutcome[2];
        for (int order = 0; order < 2; ++order) {
            const int mode = order == 0 ? firstMode : 1 - firstMode;
            document->ClearSelection();
            view.SetHitTestBoundsFilterEnabledForProfiling(mode == 1);
            view.ResetHitTestProfile();

            QElapsedTimer eventTimer;
            eventTimer.start();
            SendClick(&view, viewportQuery);
            const qreal elapsedMilliseconds =
                    eventTimer.nsecsElapsed() / 1000000.0;
            pairedOutcome[mode] = document->GetSelectedItemIds();
            if (pairedOutcome[mode] != expectedSelection) {
                output << "ERROR,bounds_ab_selection_mismatch,"
                       << entityCount << ',' << workload << ',' << mode
                       << ',' << sample << '\n';
                return false;
            }

            const CanvasView::CanvasHitTestProfile profile =
                    view.GetHitTestProfile();
            const quint64 expectedPathsPerHitTest = mode == 0
                    ? static_cast<quint64>(entityCount)
                    : static_cast<quint64>(expectedBoundsCandidates);
            const quint64 expectedPathCollectionCalls = mode == 0
                    ? static_cast<quint64>(entityCount) * 2
                    : expectedPathsPerHitTest * 2;
            const quint64 expectedBoundsChecks = mode == 0
                    ? 0 : static_cast<quint64>(entityCount) * 2;
            const quint64 expectedBoundsRejects = mode == 0
                    ? 0 : static_cast<quint64>(entityCount
                                               - expectedBoundsCandidates) * 2;
            bool entityTypeProfileMatches = true;
            if (entityTypeScoped) {
                const int typeIndex = static_cast<int>(entityType);
                const int selectionPathsPerEntity =
                        complexitySelectionPathsPerEntity >= 0
                        ? complexitySelectionPathsPerEntity
                        : BoundsEntitySelectionPathsPerEntity(entityType);
                const quint64 expectedEntitiesWithPaths =
                        expectedPathsPerHitTest * 2;
                const quint64 expectedSelectionPathCount =
                        expectedEntitiesWithPaths
                        * static_cast<quint64>(selectionPathsPerEntity);
                entityTypeProfileMatches =
                        typeIndex >= 0
                        && typeIndex
                            < CanvasView::CanvasHitTestProfile::EntityTypeCount
                        && profile.entityCountByType[typeIndex]
                            == expectedEntitiesWithPaths
                        && profile.selectionPathCountByType[typeIndex]
                            == expectedSelectionPathCount;
            }
            if (profile.hitTestCallCount != 2
                    || profile.itemIdVisitCount
                            != static_cast<quint64>(entityCount) * 2
                    || profile.pathCollectionCallCount
                            != expectedPathCollectionCalls
                    || profile.boundsCheckCount != expectedBoundsChecks
                    || profile.boundsRejectCount != expectedBoundsRejects
                    || !entityTypeProfileMatches) {
                output << "ERROR,bounds_ab_profile_invariant_failed,"
                       << entityCount << ',' << workload << ',' << mode
                       << ',' << sample << ','
                       << profile.hitTestCallCount << ','
                       << profile.itemIdVisitCount << ','
                       << profile.pathCollectionCallCount << ','
                       << profile.boundsCheckCount << ','
                       << profile.boundsRejectCount << '\n';
                return false;
            }

            BoundsModeSamples &samples = modes[mode];
            samples.eventMilliseconds.append(elapsedMilliseconds);
            samples.pathCollectionMilliseconds.append(
                        profile.pathCollectionNanoseconds / 1000000.0 / 2.0);
            samples.boundsCheckMilliseconds.append(
                        profile.boundsCheckNanoseconds / 1000000.0 / 2.0);
            samples.pathCollectionCalls += profile.pathCollectionCallCount;
            samples.itemIdVisits += profile.itemIdVisitCount;
            samples.boundsChecks += profile.boundsCheckCount;
            samples.boundsRejects += profile.boundsRejectCount;
            samples.hitTestCalls += profile.hitTestCallCount;
            samples.distanceSegmentCount += profile.distanceSegmentCount;
            for (int typeIndex = 0;
                 typeIndex < CanvasView::CanvasHitTestProfile::EntityTypeCount;
                 ++typeIndex) {
                samples.selectionPathCount +=
                        profile.selectionPathCountByType[typeIndex];
                samples.selectionPathPointCount +=
                        profile.selectionPathPointCountByType[typeIndex];
            }
        }
        if (pairedOutcome[0] != pairedOutcome[1]) {
            output << "ERROR,bounds_ab_paired_selection_mismatch,"
                   << entityCount << ',' << workload << ','
                   << sample << '\n';
            return false;
        }
    }

    for (int mode = 0; mode < 2; ++mode) {
        BoundsModeSamples &samples = modes[mode];
        BenchmarkResult result;
        result.scenario = entityTypeScoped
                ? (complexityLabel.isEmpty()
                   ? QStringLiteral("MG010_BoundsFilterEntityTypeAba")
                   : QStringLiteral("MG010_BoundsFilterComplexityAba"))
                : QStringLiteral("MG010_BoundsFilterAba");
        result.entityCount = entityCount;
        result.curvePercent = curvePercent;
        result.workload = workload + (mode == 0
                ? QStringLiteral("_filter_off")
                : QStringLiteral("_filter_on"));
        result.iterationsPerSample = 1;
        QVector<qreal> sortedEventTimes = samples.eventMilliseconds;
        std::sort(sortedEventTimes.begin(), sortedEventTimes.end());
        result.medianMilliseconds = Percentile(sortedEventTimes, 0.50);
        result.p95Milliseconds = Percentile(sortedEventTimes, 0.95);
        PrintResult(output, result);

        QVector<qreal> sortedPathTimes = samples.pathCollectionMilliseconds;
        std::sort(sortedPathTimes.begin(), sortedPathTimes.end());
        QVector<qreal> sortedBoundsTimes = samples.boundsCheckMilliseconds;
        std::sort(sortedBoundsTimes.begin(), sortedBoundsTimes.end());
        output << "# MG010_BOUNDS_AB," << entityCount << ','
               << curvePercent << ',' << workload
               << ",mode=" << (mode == 0 ? "filter_off" : "filter_on")
               << ",item_visits_per_hit_test="
               << QString::number(static_cast<qreal>(samples.itemIdVisits)
                                  / samples.hitTestCalls, 'f', 3)
               << ",path_calls_per_hit_test="
               << QString::number(static_cast<qreal>(samples.pathCollectionCalls)
                                  / samples.hitTestCalls, 'f', 3)
               << ",bounds_checks_per_hit_test="
               << QString::number(static_cast<qreal>(samples.boundsChecks)
                                  / samples.hitTestCalls, 'f', 3)
               << ",bounds_rejects_per_hit_test="
               << QString::number(static_cast<qreal>(samples.boundsRejects)
                                  / samples.hitTestCalls, 'f', 3)
               << ",selection_paths_per_hit_test="
               << QString::number(static_cast<qreal>(
                                      samples.selectionPathCount)
                                  / samples.hitTestCalls, 'f', 3)
               << ",selection_path_points_per_hit_test="
               << QString::number(static_cast<qreal>(
                                      samples.selectionPathPointCount)
                                  / samples.hitTestCalls, 'f', 3)
               << ",distance_segments_per_hit_test="
               << QString::number(static_cast<qreal>(
                                      samples.distanceSegmentCount)
                                  / samples.hitTestCalls, 'f', 3)
               << ",scene_bounds_candidates=" << expectedBoundsCandidates
               << ",scene_bounds_candidate_percent="
               << QString::number(static_cast<qreal>(expectedBoundsCandidates)
                                  * 100.0 / entityCount, 'f', 3)
               << ",requested_candidate_percent=" << candidatePercent
               << ",entity_type="
               << (entityTypeScoped ? CanvasEntityTypeName(entityType)
                                    : QStringLiteral("mixed"))
               << ",selection_paths_per_entity="
               << (entityTypeScoped
                   ? (complexitySelectionPathsPerEntity >= 0
                      ? complexitySelectionPathsPerEntity
                      : BoundsEntitySelectionPathsPerEntity(entityType))
                   : 0)
               << ",complexity="
               << (complexityLabel.isEmpty()
                   ? QStringLiteral("none") : complexityLabel)
               << ",measured_samples=" << kMeasuredSamples
               << ",path_collection_ms_per_hit_test_median="
               << QString::number(Percentile(sortedPathTimes, 0.50), 'f', 6)
               << ",path_collection_ms_per_hit_test_p95="
               << QString::number(Percentile(sortedPathTimes, 0.95), 'f', 6)
               << ",bounds_check_ms_per_hit_test_median="
               << QString::number(Percentile(sortedBoundsTimes, 0.50), 'f', 6)
               << ",bounds_check_ms_per_hit_test_p95="
               << QString::number(Percentile(sortedBoundsTimes, 0.95), 'f', 6)
               << ",selection_parity=true\n";
    }
    return true;
}

QVector<qreal> BuildClampedKnots(int controlPointCount, int degree)
{
    QVector<qreal> knots;
    knots.reserve(controlPointCount + degree + 1);
    for (int index = 0; index <= degree; ++index) {
        knots.append(0.0);
    }
    const int interiorKnotCount = controlPointCount - degree - 1;
    for (int index = 1; index <= interiorKnotCount; ++index) {
        knots.append(static_cast<qreal>(index)
                     / static_cast<qreal>(interiorKnotCount + 1));
    }
    for (int index = 0; index <= degree; ++index) {
        knots.append(1.0);
    }
    return knots;
}

QVector<CanvasVector> BoundsComplexityTriangle(const CanvasVector &center)
{
    return QVector<CanvasVector>()
            << CanvasVector(center.GetX() - 20.0, center.GetY() - 12.0)
            << CanvasVector(center.GetX() + 20.0, center.GetY() - 12.0)
            << CanvasVector(center.GetX(), center.GetY() + 24.0);
}

CanvasVector InterpolateBoundsComplexityEdge(const CanvasVector &start,
                                              const CanvasVector &end,
                                              qreal fraction)
{
    return CanvasVector(start.GetX() + (end.GetX() - start.GetX()) * fraction,
                        start.GetY() + (end.GetY() - start.GetY()) * fraction);
}

bool AddBoundsComplexityPolyline(CanvasDocument *document,
                                 const CanvasVector &center,
                                 bool exactHit,
                                 int segmentCount)
{
    if (!document || !center.IsValid() || segmentCount < 3) {
        return false;
    }

    const QVector<CanvasVector> triangle = BoundsComplexityTriangle(center);
    QVector<CanvasVector> vertices;
    vertices.reserve(segmentCount + 1);
    for (int index = 0; index <= segmentCount; ++index) {
        const qreal progress = static_cast<qreal>(index) / segmentCount;
        if (exactHit) {
            vertices.append(CanvasVector(center.GetX() - 16.0
                                         + 32.0 * progress,
                                         center.GetY()));
            continue;
        }

        if (index == segmentCount) {
            vertices.append(triangle.first());
            continue;
        }
        const qreal edgeProgress = progress * triangle.size();
        const int edgeIndex = static_cast<int>(std::floor(edgeProgress));
        const qreal edgeFraction = edgeProgress - edgeIndex;
        vertices.append(InterpolateBoundsComplexityEdge(
                            triangle.at(edgeIndex),
                            triangle.at((edgeIndex + 1) % triangle.size()),
                            edgeFraction));
    }

    CanvasPolyline polyline;
    for (int index = 1; index < vertices.size(); ++index) {
        CanvasLine segment(vertices.at(index - 1), vertices.at(index));
        if (!SetBoundsBenchmarkDisplaySize(&segment)
                || !polyline.AddSegment(segment)) {
            return false;
        }
    }
    return SetBoundsBenchmarkDisplaySize(&polyline)
            && document->AddEntity(polyline) != 0;
}

bool ConfigureBoundsComplexitySpline(CanvasSpline *spline,
                                     const CanvasVector &center,
                                     bool exactHit,
                                     int controlPointCount)
{
    if (!spline || !center.IsValid() || controlPointCount < 3) {
        return false;
    }

    const QVector<CanvasVector> triangle = BoundsComplexityTriangle(center);
    QVector<CanvasVector> controlPoints;
    controlPoints.reserve(controlPointCount);
    for (int index = 0; index < controlPointCount; ++index) {
        const qreal progress = static_cast<qreal>(index)
                / (controlPointCount - 1);
        if (exactHit) {
            controlPoints.append(CanvasVector(center.GetX() - 16.0
                                              + 32.0 * progress,
                                              center.GetY()));
            continue;
        }

        const qreal edgeProgress = progress * 2.0;
        const int edgeIndex = qMin(1, static_cast<int>(
                                       std::floor(edgeProgress)));
        const qreal edgeFraction = edgeProgress - edgeIndex;
        controlPoints.append(InterpolateBoundsComplexityEdge(
                                 triangle.at(edgeIndex),
                                 triangle.at(edgeIndex + 1), edgeFraction));
    }

    return spline->SetData(1, controlPoints,
                           BuildClampedKnots(controlPointCount, 1),
                           QVector<qreal>(), false, false);
}

bool AddBoundsComplexityGroup(CanvasDocument *document,
                              const CanvasVector &center,
                              bool exactHit,
                              int childCount)
{
    if (!document || !center.IsValid() || childCount < 3) {
        return false;
    }

    const QVector<CanvasVector> triangle = BoundsComplexityTriangle(center);
    CanvasGroupContainer group;
    for (int index = 0; index < childCount; ++index) {
        CanvasLine child;
        bool geometrySet = false;
        if (exactHit && index == 0) {
            geometrySet = child.SetGeometry(
                        CanvasVector(center.GetX() - 16.0, center.GetY()),
                        CanvasVector(center.GetX() + 16.0, center.GetY()));
        } else {
            const int edgeIndex = (exactHit ? index - 1 : index) % 3;
            geometrySet = child.SetGeometry(triangle.at(edgeIndex),
                                             triangle.at((edgeIndex + 1) % 3));
        }
        if (!geometrySet || !SetBoundsBenchmarkDisplaySize(&child)
                || !group.AddChild(child)) {
            return false;
        }
    }

    return SetBoundsBenchmarkDisplaySize(&group)
            && document->AddEntity(group) != 0;
}

bool AddBoundsComplexityEntity(CanvasDocument *document,
                               CanvasEntityType type,
                               const CanvasVector &center,
                               bool exactHit,
                               int complexity)
{
    switch (type) {
    case CanvasEntityType::Polyline:
        return AddBoundsComplexityPolyline(document, center, exactHit,
                                           complexity);
    case CanvasEntityType::Spline: {
        CanvasSpline spline;
        return ConfigureBoundsComplexitySpline(&spline, center, exactHit,
                                               complexity)
                && SetBoundsBenchmarkDisplaySize(&spline)
                && document->AddEntity(spline) != 0;
    }
    case CanvasEntityType::GroupContainer:
        return AddBoundsComplexityGroup(document, center, exactHit, complexity);
    case CanvasEntityType::Invalid:
    case CanvasEntityType::Point:
    case CanvasEntityType::Line:
    case CanvasEntityType::Arc:
    case CanvasEntityType::Circle:
    case CanvasEntityType::Ellipse:
        return false;
    }
    return false;
}

bool PopulateBoundsComplexityDocument(CanvasDocument *document,
                                     CanvasEntityType type,
                                     int entityCount,
                                     int candidatePercent,
                                     int complexity,
                                     QString *failureReason = nullptr)
{
    if (!document || entityCount <= 0 || complexity < 3
            || candidatePercent <= 0 || candidatePercent > 100
            || (entityCount * candidatePercent) % 100 != 0) {
        if (failureReason) {
            *failureReason = QStringLiteral("invalid_parameters");
        }
        return false;
    }

    const int candidateCount = entityCount * candidatePercent / 100;
    if (candidateCount <= 0 || candidateCount > entityCount) {
        if (failureReason) {
            *failureReason = QStringLiteral("invalid_candidate_count");
        }
        return false;
    }

    for (int index = 0; index < entityCount; ++index) {
        const bool isCandidate = index < candidateCount;
        const CanvasVector center = isCandidate
                ? kInteractionProbeCenter : EntityPosition(index);
        if (!AddBoundsComplexityEntity(document, type, center, index == 0,
                                       complexity)) {
            if (failureReason) {
                *failureReason = QStringLiteral("entity_add_failed_at_%1")
                        .arg(index);
            }
            return false;
        }
    }

    if (document->GetItemCount() != entityCount) {
        if (failureReason) {
            *failureReason = QStringLiteral("item_count_%1_expected_%2")
                    .arg(document->GetItemCount()).arg(entityCount);
        }
        return false;
    }
    for (const ItemId id : document->GetItemIds()) {
        const CanvasEntity *entity = document->GetEntity(id);
        if (!entity || entity->GetType() != type) {
            if (failureReason) {
                *failureReason = QStringLiteral("entity_type_mismatch_for_id_%1")
                        .arg(static_cast<qulonglong>(id));
            }
            return false;
        }
    }
    return true;
}

bool PopulateSplineProfileDocument(CanvasDocument *document,
                                   int controlPointCount,
                                   bool rational,
                                   int splineCount)
{
    if (!document || controlPointCount < 3 || splineCount <= 0) {
        return false;
    }

    const int degree = qMin(3, controlPointCount - 1);
    const QVector<qreal> knots = BuildClampedKnots(controlPointCount, degree);
    for (int splineIndex = 0; splineIndex < splineCount; ++splineIndex) {
        const qreal offset = static_cast<qreal>(splineIndex) * 400.0;
        QVector<CanvasVector> controlPoints;
        QVector<qreal> weights;
        controlPoints.reserve(controlPointCount);
        if (rational) weights.reserve(controlPointCount);
        for (int index = 0; index < controlPointCount; ++index) {
            const qreal parameter = static_cast<qreal>(index);
            controlPoints.append(CanvasVector(
                                     offset + parameter * 2.0,
                                     std::sin(parameter * 0.37) * 8.0));
            if (rational) {
                weights.append(0.5 + static_cast<qreal>(index % 5) * 0.25);
            }
        }

        CanvasSpline spline;
        if (!spline.SetData(degree, controlPoints, knots, weights,
                            false, false)
                || document->AddEntity(spline) == 0) {
            return false;
        }
    }
    return document->GetItemCount() == splineCount;
}

bool SetProfileZoom(CanvasView *view, int wheelSteps)
{
    if (!view || wheelSteps < 0) return false;
    const QPointF center(view->width() * 0.5, view->height() * 0.5);
    for (int step = 0; step < wheelSteps; ++step) {
        QWheelEvent wheel(center, center, QPoint(0, 0), QPoint(0, 120),
                          Qt::NoButton, Qt::NoModifier, Qt::ScrollUpdate,
                          false);
        QApplication::sendEvent(view, &wheel);
    }
    return view->GetZoom() > 0.0;
}

bool MeasureSplinePathBuild(QTextStream &output,
                            int controlPointCount,
                            bool rational,
                            int wheelSteps)
{
    const int splineCount = 36;
    CanvasDocument document;
    if (!PopulateSplineProfileDocument(&document, controlPointCount,
                                       rational, splineCount)) {
        output << "ERROR,spline_profile_scene_build_failed,"
               << controlPointCount << ',' << rational << '\n';
        return false;
    }

    CanvasView view;
    ConfigureView(&view, &document, false);
    view.setUpdatesEnabled(false);
    const CanvasVector missPosition(1000000.0, 1000000.0);
    view.CenterOnWorld(missPosition);
    if (!SetProfileZoom(&view, wheelSteps)) {
        output << "ERROR,spline_profile_zoom_failed," << wheelSteps << '\n';
        return false;
    }
    view.SetHitTestBoundsFilterEnabledForProfiling(false);
    const QPointF query = view.MapFromWorld(missPosition);
    for (int sample = 0; sample < kWarmupSamples; ++sample) {
        document.ClearSelection();
        SendClick(&view, query);
    }

    QVector<qreal> eventMilliseconds;
    QVector<qreal> pathBuildMillisecondsPerSpline;
    quint64 pathPointsPerSpline = 0;
    for (int sample = 0; sample < kMeasuredSamples; ++sample) {
        document.ClearSelection();
        view.ResetHitTestProfile();
        QElapsedTimer timer;
        timer.start();
        SendClick(&view, query);
        eventMilliseconds.append(timer.nsecsElapsed() / 1000000.0);

        const CanvasView::CanvasHitTestProfile profile =
                view.GetHitTestProfile();
        if (document.GetSelectedItemCount() != 0
                || profile.hitTestCallCount != 2
                || profile.pathCollectionCallCount
                        != static_cast<quint64>(splineCount) * 2
                || profile.selectionPathCountByType[
                        static_cast<int>(CanvasEntityType::Spline)] == 0
                || profile.pathCollectionNanosecondsByType[
                        static_cast<int>(CanvasEntityType::Spline)] <= 0) {
            output << "ERROR,spline_profile_invariant_failed,"
                   << controlPointCount << ',' << rational << ','
                   << wheelSteps << ',' << profile.hitTestCallCount << ','
                   << profile.pathCollectionCallCount << '\n';
            return false;
        }

        const int splineTypeIndex = static_cast<int>(CanvasEntityType::Spline);
        const qreal perSplineMilliseconds =
                static_cast<qreal>(profile.pathCollectionNanosecondsByType[
                        splineTypeIndex])
                / 1000000.0 / profile.hitTestCallCount / splineCount;
        pathBuildMillisecondsPerSpline.append(perSplineMilliseconds);
        pathPointsPerSpline = static_cast<quint64>(
                    profile.selectionPathPointCountByType[splineTypeIndex]
                    / profile.hitTestCallCount / splineCount);
    }

    const qreal zoom = view.GetZoom();
    const qreal maximumWorldError = qMax<qreal>(1.0e-6, 0.5 / zoom);
    BenchmarkResult result;
    result.scenario = QStringLiteral("MG010_SplineSelectionPathBuild");
    result.entityCount = splineCount;
    result.curvePercent = 100;
    result.workload = QStringLiteral("control_points_%1_degree_%2_%3_zoom_steps_%4")
            .arg(controlPointCount)
            .arg(qMin(3, controlPointCount - 1))
            .arg(rational ? QStringLiteral("rational")
                          : QStringLiteral("polynomial"))
            .arg(wheelSteps);
    result.iterationsPerSample = 1;
    QVector<qreal> sortedEventTimes = eventMilliseconds;
    std::sort(sortedEventTimes.begin(), sortedEventTimes.end());
    result.medianMilliseconds = Percentile(sortedEventTimes, 0.50);
    result.p95Milliseconds = Percentile(sortedEventTimes, 0.95);
    PrintResult(output, result);

    QVector<qreal> sortedPathTimes = pathBuildMillisecondsPerSpline;
    std::sort(sortedPathTimes.begin(), sortedPathTimes.end());
    output << "# MG010_SPLINE_PROFILE,control_points=" << controlPointCount
           << ",degree=" << qMin(3, controlPointCount - 1)
           << ",rational=" << (rational ? "true" : "false")
           << ",zoom=" << QString::number(zoom, 'f', 6)
           << ",maximum_world_error="
           << QString::number(maximumWorldError, 'f', 8)
           << ",splines_per_hit_test=" << splineCount
           << ",path_points_per_spline=" << pathPointsPerSpline
           << ",path_build_ms_per_spline_median="
           << QString::number(Percentile(sortedPathTimes, 0.50), 'f', 6)
           << ",path_build_ms_per_spline_p95="
           << QString::number(Percentile(sortedPathTimes, 0.95), 'f', 6)
           << ",query=miss,bounds_filter=false\n";
    return true;
}

bool RunMG010Experiment(QTextStream &output)
{
    if (!VerifyBoundsFilterHitParity(output)) {
        return false;
    }

    const int entityCounts[] = {100, 1000, 5000};
    const SceneMix mixes[] = {SceneMix::Simple, SceneMix::CurveHeavy};
    output << "# MG010_BOUNDS_MATRIX,entity_counts=100|1000|5000"
              ",mixes=simple|curve_heavy"
              ",workloads=sparse_hit|miss|dense_overlap_hit"
              ",sample_order=alternating_filter_off_on\n";
    for (SceneMix mix : mixes) {
        for (int entityCount : entityCounts) {
            CanvasDocument sparseDocument;
            if (!PopulateDocument(&sparseDocument, entityCount, mix)
                    || !PrepareInteractionProbe(&sparseDocument)
                    || !MeasureBoundsFilterAba(
                        output, &sparseDocument, entityCount, mix,
                        BoundsFilterWorkload::SparseHit)
                    || !MeasureBoundsFilterAba(
                        output, &sparseDocument, entityCount, mix,
                        BoundsFilterWorkload::Miss)) {
                output << "ERROR,bounds_filter_sparse_matrix_failed,"
                       << entityCount << ',' << CurvePercentage(mix) << '\n';
                return false;
            }

            CanvasDocument denseDocument;
            if (!PopulateDocument(&denseDocument, entityCount, mix, true)
                    || !MeasureBoundsFilterAba(
                        output, &denseDocument, entityCount, mix,
                        BoundsFilterWorkload::DenseHit)) {
                output << "ERROR,bounds_filter_dense_matrix_failed,"
                       << entityCount << ',' << CurvePercentage(mix) << '\n';
                return false;
            }
        }
    }

    const int selectivityEntityCounts[] = {1000, 5000};
    const int candidatePercentages[] = {1, 10, 25, 50, 100};
    const int nearMissPercentages[] = {25, 100};
    output << "# MG010_BOUNDS_SELECTIVITY_MATRIX"
              ",entity_counts=1000|5000"
              ",mixes=simple|curve_heavy"
              ",controlled_types=simple_line|curve_heavy_2line_4curve"
              ",point_excluded=true"
              ",geometry_size_fixed_across_candidate_states=true"
              ",hit_candidate_percentages=1|10|25|50|100"
              ",near_miss_candidate_percentages=25|100"
              ",near_miss_means_aabb_candidates_but_no_exact_hit"
              ",sample_order=alternating_filter_off_on\n";
    for (SceneMix mix : mixes) {
        for (int entityCount : selectivityEntityCounts) {
            for (int candidatePercent : candidatePercentages) {
                CanvasDocument document;
                if (!PopulateBoundsSelectivityDocument(
                            &document, entityCount, mix, candidatePercent, true)) {
                    output << "ERROR,bounds_selectivity_hit_scene_build_failed,"
                           << entityCount << ',' << CurvePercentage(mix) << ','
                           << candidatePercent << '\n';
                    return false;
                }
                output << "# MG010_BOUNDS_SELECTIVITY_CASE,"
                       << "entities=" << entityCount
                       << ",curve_percent=" << CurvePercentage(mix)
                       << ",case=exact_hit"
                       << ",requested_candidate_percent=" << candidatePercent
                       << ",expected_candidate_count="
                       << entityCount * candidatePercent / 100 << '\n';
                if (!MeasureBoundsFilterAba(
                            output, &document, entityCount, mix,
                            BoundsFilterWorkload::SelectivityHit,
                            candidatePercent)) {
                    output << "ERROR,bounds_selectivity_hit_measurement_failed,"
                           << entityCount << ',' << CurvePercentage(mix) << ','
                           << candidatePercent << '\n';
                    return false;
                }
            }

            for (int candidatePercent : nearMissPercentages) {
                CanvasDocument document;
                if (!PopulateBoundsSelectivityDocument(
                            &document, entityCount, mix, candidatePercent, false)) {
                    output << "ERROR,bounds_selectivity_near_miss_scene_build_failed,"
                           << entityCount << ',' << CurvePercentage(mix) << ','
                           << candidatePercent << '\n';
                    return false;
                }
                output << "# MG010_BOUNDS_SELECTIVITY_CASE,"
                       << "entities=" << entityCount
                       << ",curve_percent=" << CurvePercentage(mix)
                       << ",case=aabb_overlap_exact_miss"
                       << ",requested_candidate_percent=" << candidatePercent
                       << ",expected_candidate_count="
                       << entityCount * candidatePercent / 100 << '\n';
                if (!MeasureBoundsFilterAba(
                            output, &document, entityCount, mix,
                            BoundsFilterWorkload::BoundsNearMiss,
                            candidatePercent)) {
                    output << "ERROR,bounds_selectivity_near_miss_measurement_failed,"
                           << entityCount << ',' << CurvePercentage(mix) << ','
                           << candidatePercent << '\n';
                    return false;
                }
            }
        }
    }
    output << "# MG010_BOUNDS_SELECTIVITY_SUMMARY,"
              "hit_candidate_rates=1|10|25|50|100"
              ",near_miss_rates=25|100"
              ",geometry_size_fixed_across_candidate_states=true"
              ",filter_off_on_selection_parity=true"
              ",observed_candidate_counts_verified=true\n";

    const CanvasEntityType boundsEntityTypes[] = {
        CanvasEntityType::Point,
        CanvasEntityType::Line,
        CanvasEntityType::Arc,
        CanvasEntityType::Circle,
        CanvasEntityType::Ellipse,
        CanvasEntityType::Polyline,
        CanvasEntityType::Spline,
        CanvasEntityType::GroupContainer
    };
    output << "# MG010_BOUNDS_ENTITY_TYPE_MATRIX"
              ",entity_types=Point|Line|Arc|Circle|Ellipse|Polyline|Spline|GroupContainer"
              ",entity_counts=1000|5000"
              ",candidate_percentages=1|10|25|50|100"
              ",each_scene_contains_one_exact_hit_and_remaining_candidates_are_aabb_false_positives"
              ",non_candidate_geometry_matches_candidate_false_positive_geometry=true"
              ",polyline_segments=3"
              ",group_children=3_point_line_spline"
              ",sample_order=alternating_filter_off_on\n";
    for (CanvasEntityType type : boundsEntityTypes) {
        for (int entityCount : selectivityEntityCounts) {
            for (int candidatePercent : candidatePercentages) {
                CanvasDocument document;
                QString sceneFailureReason;
                if (!PopulateBoundsEntityTypeDocument(
                            &document, type, entityCount, candidatePercent,
                            &sceneFailureReason)) {
                    output << "ERROR,bounds_entity_type_scene_build_failed,"
                           << CanvasEntityTypeName(type) << ',' << entityCount
                           << ',' << candidatePercent << ','
                           << sceneFailureReason << '\n';
                    return false;
                }

                const QVector<ItemId> ids = document.GetItemIds();
                const ItemId expectedHitId = ids.isEmpty() ? 0 : ids.first();
                if (expectedHitId == 0) {
                    output << "ERROR,bounds_entity_type_hit_id_missing,"
                           << CanvasEntityTypeName(type) << ',' << entityCount
                           << ',' << candidatePercent << '\n';
                    return false;
                }

                output << "# MG010_BOUNDS_ENTITY_TYPE_CASE,entity_type="
                       << CanvasEntityTypeName(type)
                       << ",entities=" << entityCount
                       << ",candidate_percent=" << candidatePercent
                       << ",expected_candidate_count="
                       << entityCount * candidatePercent / 100
                       << ",expected_exact_hit_id=" << expectedHitId
                       << ",selection_paths_per_entity="
                       << BoundsEntitySelectionPathsPerEntity(type) << '\n';
                if (!MeasureBoundsFilterAba(
                            output, &document, entityCount, SceneMix::Simple,
                            BoundsFilterWorkload::SelectivityHit,
                            candidatePercent, type, expectedHitId)) {
                    output << "ERROR,bounds_entity_type_measurement_failed,"
                           << CanvasEntityTypeName(type) << ',' << entityCount
                           << ',' << candidatePercent << '\n';
                    return false;
                }
            }
        }
    }
    output << "# MG010_BOUNDS_ENTITY_TYPE_SUMMARY,"
              "entity_type_cases=80"
              ",filter_modes=off|on"
              ",filter_off_on_selection_parity=true"
              ",expected_exact_hit_identity_verified=true"
              ",entity_type_path_counts_verified=true"
              ",candidate_counts_verified=true\n";

    const int controlPointCounts[] = {3, 16, 64};
    const int zoomSteps[] = {0, 18};
    for (int controlPointCount : controlPointCounts) {
        for (int steps : zoomSteps) {
            if (!MeasureSplinePathBuild(output, controlPointCount,
                                        false, steps)) {
                return false;
            }
        }
    }
    if (!MeasureSplinePathBuild(output, 64, true, 18)) {
        return false;
    }
    output.flush();
    return true;
}

bool RunMG010ComplexityExperiment(QTextStream &output)
{
    if (!VerifyBoundsFilterHitParity(output)) {
        return false;
    }

    struct ComplexityCase
    {
        CanvasEntityType entityType;
        int complexity;
        const char *dimension;
        int selectionPathsPerEntity;
    };
    const ComplexityCase cases[] = {
        {CanvasEntityType::Polyline, 3, "polyline_segments", 3},
        {CanvasEntityType::Polyline, 16, "polyline_segments", 16},
        {CanvasEntityType::Polyline, 64, "polyline_segments", 64},
        {CanvasEntityType::Polyline, 256, "polyline_segments", 256},
        {CanvasEntityType::Spline, 3, "spline_control_points", 1},
        {CanvasEntityType::Spline, 16, "spline_control_points", 1},
        {CanvasEntityType::Spline, 64, "spline_control_points", 1},
        {CanvasEntityType::GroupContainer, 3, "group_children", 3},
        {CanvasEntityType::GroupContainer, 16, "group_children", 16},
        {CanvasEntityType::GroupContainer, 64, "group_children", 64}
    };
    const int entityCount = 5000;
    const int candidatePercentages[] = {1, 25, 50, 100};
    const int candidatePercentageCount =
            sizeof(candidatePercentages) / sizeof(candidatePercentages[0]);
    const int caseCount = sizeof(cases) / sizeof(cases[0]);
    int completedCaseCount = 0;

    output << "# MG010_BOUNDS_COMPLEXITY_MATRIX"
              ",entity_count=5000"
              ",entity_types=Polyline|Spline|GroupContainer"
              ",polyline_segments=3|16|64|256"
              ",spline_control_points=3|16|64"
              ",group_children=3|16|64"
              ",candidate_percentages=1|25|50|100"
              ",samples_per_mode=31"
              ",sample_order=alternating_filter_off_on"
              ",same_complexity_for_candidate_and_non_candidate_entities=true\n";

    for (int caseIndex = 0; caseIndex < caseCount; ++caseIndex) {
        const ComplexityCase &complexityCase = cases[caseIndex];
        const QString complexityLabel = QStringLiteral("%1_%2")
                .arg(QString::fromLatin1(complexityCase.dimension))
                .arg(complexityCase.complexity);
        for (int candidateIndex = 0;
             candidateIndex < candidatePercentageCount;
             ++candidateIndex) {
            const int candidatePercent =
                    candidatePercentages[candidateIndex];
            CanvasDocument document;
            QString sceneFailureReason;
            if (!PopulateBoundsComplexityDocument(
                        &document, complexityCase.entityType, entityCount,
                        candidatePercent, complexityCase.complexity,
                        &sceneFailureReason)) {
                output << "ERROR,bounds_complexity_scene_build_failed,"
                       << CanvasEntityTypeName(complexityCase.entityType)
                       << ',' << complexityLabel << ',' << candidatePercent
                       << ',' << sceneFailureReason << '\n';
                return false;
            }

            const QVector<ItemId> ids = document.GetItemIds();
            const ItemId expectedHitId = ids.isEmpty() ? 0 : ids.first();
            if (expectedHitId == 0) {
                output << "ERROR,bounds_complexity_hit_id_missing,"
                       << CanvasEntityTypeName(complexityCase.entityType)
                       << ',' << complexityLabel << ',' << candidatePercent
                       << '\n';
                return false;
            }

            output << "# MG010_BOUNDS_COMPLEXITY_CASE,entity_type="
                   << CanvasEntityTypeName(complexityCase.entityType)
                   << ",dimension=" << complexityCase.dimension
                   << ",complexity=" << complexityCase.complexity
                   << ",entities=" << entityCount
                   << ",candidate_percent=" << candidatePercent
                   << ",expected_candidate_count="
                   << entityCount * candidatePercent / 100
                   << ",expected_exact_hit_id=" << expectedHitId
                   << ",selection_paths_per_entity="
                   << complexityCase.selectionPathsPerEntity << '\n';
            output.flush();

            if (!MeasureBoundsFilterAba(
                        output, &document, entityCount, SceneMix::Simple,
                        BoundsFilterWorkload::SelectivityHit,
                        candidatePercent, complexityCase.entityType,
                        expectedHitId, complexityLabel,
                        complexityCase.selectionPathsPerEntity)) {
                output << "ERROR,bounds_complexity_measurement_failed,"
                       << CanvasEntityTypeName(complexityCase.entityType)
                       << ',' << complexityLabel << ',' << candidatePercent
                       << '\n';
                return false;
            }
            ++completedCaseCount;
        }
    }

    output << "# MG010_BOUNDS_COMPLEXITY_SUMMARY,cases="
           << completedCaseCount
           << ",timing_rows=" << completedCaseCount * 2
           << ",entity_count=" << entityCount
           << ",candidate_percentages=1|25|50|100"
           << ",filter_off_on_selection_parity=true"
           << ",candidate_counts_verified=true"
           << ",selection_path_counts_verified=true"
           << ",selection_path_points_and_distance_segments_recorded=true\n";
    output.flush();
    return completedCaseCount == caseCount * candidatePercentageCount;
}

struct BoundsCacheComplexityCase
{
    CanvasEntityType entityType;
    int complexity;
    const char *dimension;
};

struct BoundsCacheQuerySamples
{
    QVector<qreal> recomputeMilliseconds;
    QVector<qreal> cachedMilliseconds;
    QVector<qreal> savingsMilliseconds;
};

struct BoundsCacheMutationSamples
{
    QVector<qreal> mutationOnlyMilliseconds;
    QVector<qreal> mutationAndRefreshMilliseconds;
    QVector<qreal> refreshOverheadMilliseconds;
};

bool BuildBoundsCache(const CanvasDocument &document,
                      const QVector<ItemId> &ids,
                      QHash<ItemId, QRectF> *boundsCache)
{
    if (!boundsCache) {
        return false;
    }

    boundsCache->clear();
    boundsCache->reserve(ids.size());
    for (const ItemId id : ids) {
        const CanvasEntity *entity = document.GetEntity(id);
        QRectF bounds;
        if (!entity || !TryGetGeometryBounds(*entity, &bounds)) {
            return false;
        }
        boundsCache->insert(id, bounds);
    }
    return boundsCache->size() == ids.size();
}

bool VerifyBoundsCacheMatchesDocument(
        const CanvasDocument &document,
        const QVector<ItemId> &ids,
        const QHash<ItemId, QRectF> &boundsCache)
{
    if (boundsCache.size() != ids.size()) {
        return false;
    }

    for (const ItemId id : ids) {
        const CanvasEntity *entity = document.GetEntity(id);
        QRectF currentBounds;
        const auto cachedBounds = boundsCache.constFind(id);
        if (!entity || cachedBounds == boundsCache.constEnd()
                || !TryGetGeometryBounds(*entity, &currentBounds)
                || cachedBounds.value() != currentBounds) {
            return false;
        }
    }
    return true;
}

bool CountBoundsCandidates(const CanvasDocument &document,
                           const QVector<ItemId> &ids,
                           const QHash<ItemId, QRectF> *boundsCache,
                           const CanvasVector &query,
                           int *candidateCount)
{
    if (!query.IsValid() || !candidateCount) {
        return false;
    }

    int count = 0;
    const QPointF queryPoint(query.GetX(), query.GetY());
    for (const ItemId id : ids) {
        const CanvasEntity *entity = document.GetEntity(id);
        if (!entity) {
            return false;
        }

        QRectF bounds;
        if (boundsCache) {
            const auto cachedBounds = boundsCache->constFind(id);
            if (cachedBounds == boundsCache->constEnd()) {
                return false;
            }
            bounds = cachedBounds.value();
        } else if (!TryGetGeometryBounds(*entity, &bounds)) {
            return false;
        }

        if (bounds.contains(queryPoint)) {
            ++count;
        }
    }

    *candidateCount = count;
    return true;
}

BenchmarkResult MakeBoundsCacheResult(const QString &scenario,
                                      int entityCount,
                                      int curvePercent,
                                      const QString &workload,
                                      int iterationsPerSample,
                                      QVector<qreal> samples)
{
    std::sort(samples.begin(), samples.end());
    BenchmarkResult result;
    result.scenario = scenario;
    result.entityCount = entityCount;
    result.curvePercent = curvePercent;
    result.workload = workload;
    result.iterationsPerSample = iterationsPerSample;
    result.medianMilliseconds = Percentile(samples, 0.50);
    result.p95Milliseconds = Percentile(samples, 0.95);
    return result;
}

bool MeasureBoundsCacheQueries(QTextStream &output,
                               const CanvasDocument &document,
                               const QVector<ItemId> &ids,
                               const QHash<ItemId, QRectF> &boundsCache,
                               const CanvasVector &query,
                               int expectedCandidateCount,
                               const QString &complexityLabel,
                               int curvePercent,
                               BoundsCacheQuerySamples *samples)
{
    if (ids.isEmpty() || !samples) {
        return false;
    }

    const int iterationsPerSample = 3;
    const auto runMode = [&](bool useCache, int *candidateCount) {
        return CountBoundsCandidates(document, ids,
                                     useCache ? &boundsCache : nullptr,
                                     query, candidateCount);
    };

    for (int sample = 0; sample < kWarmupSamples; ++sample) {
        const int firstMode = sample % 2;
        for (int order = 0; order < 2; ++order) {
            const bool useCache = (order == 0 ? firstMode : 1 - firstMode) != 0;
            for (int iteration = 0; iteration < iterationsPerSample; ++iteration) {
                int observedCandidateCount = 0;
                if (!runMode(useCache, &observedCandidateCount)
                        || observedCandidateCount != expectedCandidateCount) {
                    output << "ERROR,bounds_cache_warmup_parity_failed,"
                           << complexityLabel << ',' << sample << ','
                           << (useCache ? "cached" : "recompute") << ','
                           << observedCandidateCount << ','
                           << expectedCandidateCount << '\n';
                    return false;
                }
            }
        }
    }

    samples->recomputeMilliseconds.reserve(kMeasuredSamples);
    samples->cachedMilliseconds.reserve(kMeasuredSamples);
    samples->savingsMilliseconds.reserve(kMeasuredSamples);
    for (int sample = 0; sample < kMeasuredSamples; ++sample) {
        const int firstMode = sample % 2;
        int observedCandidateCount[2] = {0, 0};
        for (int order = 0; order < 2; ++order) {
            const int mode = order == 0 ? firstMode : 1 - firstMode;
            QElapsedTimer timer;
            timer.start();
            for (int iteration = 0; iteration < iterationsPerSample; ++iteration) {
                if (!runMode(mode != 0, &observedCandidateCount[mode])
                        || observedCandidateCount[mode]
                                != expectedCandidateCount) {
                    output << "ERROR,bounds_cache_query_parity_failed,"
                           << complexityLabel << ',' << sample << ','
                           << (mode != 0 ? "cached" : "recompute") << ','
                           << observedCandidateCount[mode] << ','
                           << expectedCandidateCount << '\n';
                    return false;
                }
            }
            const qreal elapsedMilliseconds =
                    timer.nsecsElapsed() / 1000000.0 / iterationsPerSample;
            if (mode == 0) {
                samples->recomputeMilliseconds.append(elapsedMilliseconds);
            } else {
                samples->cachedMilliseconds.append(elapsedMilliseconds);
            }
        }
        if (observedCandidateCount[0] != observedCandidateCount[1]) {
            output << "ERROR,bounds_cache_sample_result_mismatch,"
                   << complexityLabel << ',' << sample << '\n';
            return false;
        }
        samples->savingsMilliseconds.append(
                    samples->recomputeMilliseconds.last()
                    - samples->cachedMilliseconds.last());
    }

    const QString workload = complexityLabel
            + QStringLiteral("_sparse_aabb_candidates");
    PrintResult(output, MakeBoundsCacheResult(
                    QStringLiteral("MG010_BoundsLiveRecompute"),
                    ids.size(), curvePercent, workload, iterationsPerSample,
                    samples->recomputeMilliseconds));
    PrintResult(output, MakeBoundsCacheResult(
                    QStringLiteral("MG010_BoundsCachedRead"), ids.size(),
                    curvePercent, workload, iterationsPerSample,
                    samples->cachedMilliseconds));
    return true;
}

bool MeasureBoundsCacheMutation(QTextStream &output,
                                CanvasEntityType entityType,
                                int complexity,
                                int entityCount,
                                int candidatePercent,
                                int curvePercent,
                                const QString &complexityLabel,
                                BoundsCacheMutationSamples *samples)
{
    if (!samples) {
        return false;
    }

    CanvasDocument mutationOnlyDocument;
    CanvasDocument mutationAndRefreshDocument;
    if (!PopulateBoundsComplexityDocument(
                &mutationOnlyDocument, entityType, entityCount,
                candidatePercent, complexity)
            || !PopulateBoundsComplexityDocument(
                &mutationAndRefreshDocument, entityType, entityCount,
                candidatePercent, complexity)) {
        output << "ERROR,bounds_cache_mutation_scene_build_failed,"
               << complexityLabel << '\n';
        return false;
    }

    const QVector<ItemId> mutationOnlyIds =
            mutationOnlyDocument.GetItemIds();
    const QVector<ItemId> mutationAndRefreshIds =
            mutationAndRefreshDocument.GetItemIds();
    if (mutationOnlyIds.size() != entityCount
            || mutationAndRefreshIds.size() != entityCount) {
        output << "ERROR,bounds_cache_mutation_ids_failed,"
               << complexityLabel << '\n';
        return false;
    }

    const ItemId mutationOnlyId = mutationOnlyIds.first();
    const ItemId mutationAndRefreshId = mutationAndRefreshIds.first();
    const QVector<ItemId> mutationOnlyTargetIds = QVector<ItemId>()
            << mutationOnlyId;
    const QVector<ItemId> mutationAndRefreshTargetIds = QVector<ItemId>()
            << mutationAndRefreshId;
    QHash<ItemId, QRectF> boundsCache;
    if (!BuildBoundsCache(mutationAndRefreshDocument,
                          mutationAndRefreshIds, &boundsCache)) {
        output << "ERROR,bounds_cache_mutation_initial_cache_failed,"
               << complexityLabel << '\n';
        return false;
    }

    samples->mutationOnlyMilliseconds.reserve(kMeasuredSamples);
    samples->mutationAndRefreshMilliseconds.reserve(kMeasuredSamples);
    samples->refreshOverheadMilliseconds.reserve(kMeasuredSamples);
    int pairIndex = 0;
    const int warmupPairs = kWarmupSamples;
    for (int pair = 0; pair < warmupPairs + kMeasuredSamples; ++pair) {
        const bool moveForward = pairIndex % 2 == 0;
        ++pairIndex;
        const CanvasVector delta = moveForward
                ? CanvasVector(0.25, -0.125)
                : CanvasVector(-0.25, 0.125);
        const int firstMode = pair % 2;
        qreal elapsedMilliseconds[2] = {0.0, 0.0};
        QRectF refreshedBounds;

        for (int order = 0; order < 2; ++order) {
            const int mode = order == 0 ? firstMode : 1 - firstMode;
            QElapsedTimer timer;
            timer.start();
            bool operationSucceeded = false;
            if (mode == 0) {
                operationSucceeded = mutationOnlyDocument.TranslateItems(
                            mutationOnlyTargetIds, delta);
            } else {
                operationSucceeded = mutationAndRefreshDocument.TranslateItems(
                            mutationAndRefreshTargetIds, delta);
                const CanvasEntity *updatedEntity =
                        mutationAndRefreshDocument.GetEntity(
                            mutationAndRefreshId);
                operationSucceeded = operationSucceeded && updatedEntity
                        && TryGetGeometryBounds(*updatedEntity,
                                                &refreshedBounds);
                if (operationSucceeded) {
                    boundsCache.insert(mutationAndRefreshId,
                                       refreshedBounds);
                }
            }
            elapsedMilliseconds[mode] = timer.nsecsElapsed() / 1000000.0;
            if (!operationSucceeded) {
                output << "ERROR,bounds_cache_mutation_operation_failed,"
                       << complexityLabel << ',' << pair << ',' << mode << '\n';
                return false;
            }
        }

        const CanvasEntity *mutationOnlyEntity =
                mutationOnlyDocument.GetEntity(mutationOnlyId);
        const CanvasEntity *mutationAndRefreshEntity =
                mutationAndRefreshDocument.GetEntity(mutationAndRefreshId);
        QRectF mutationOnlyBounds;
        QRectF mutationAndRefreshBounds;
        const auto cachedBounds = boundsCache.constFind(mutationAndRefreshId);
        if (!mutationOnlyEntity || !mutationAndRefreshEntity
                || cachedBounds == boundsCache.constEnd()
                || !TryGetGeometryBounds(*mutationOnlyEntity,
                                         &mutationOnlyBounds)
                || !TryGetGeometryBounds(*mutationAndRefreshEntity,
                                         &mutationAndRefreshBounds)
                || mutationOnlyBounds != mutationAndRefreshBounds
                || cachedBounds.value() != mutationAndRefreshBounds
                || refreshedBounds != mutationAndRefreshBounds) {
            output << "ERROR,bounds_cache_mutation_refresh_parity_failed,"
                   << complexityLabel << ',' << pair << '\n';
            return false;
        }

        if (pair >= warmupPairs) {
            samples->mutationOnlyMilliseconds.append(elapsedMilliseconds[0]);
            samples->mutationAndRefreshMilliseconds.append(
                        elapsedMilliseconds[1]);
            samples->refreshOverheadMilliseconds.append(
                        elapsedMilliseconds[1] - elapsedMilliseconds[0]);
        }
    }

    if (!VerifyBoundsCacheMatchesDocument(mutationAndRefreshDocument,
                                          mutationAndRefreshIds,
                                          boundsCache)) {
        output << "ERROR,bounds_cache_mutation_final_parity_failed,"
               << complexityLabel << '\n';
        return false;
    }

    const QString workload = complexityLabel
            + QStringLiteral("_translate_one_entity");
    PrintResult(output, MakeBoundsCacheResult(
                    QStringLiteral("MG010_BoundsMutationOnly"), entityCount,
                    curvePercent, workload, 1,
                    samples->mutationOnlyMilliseconds));
    PrintResult(output, MakeBoundsCacheResult(
                    QStringLiteral("MG010_BoundsMutationPlusRefresh"),
                    entityCount, curvePercent, workload, 1,
                    samples->mutationAndRefreshMilliseconds));
    return true;
}

quint64 BoundsCacheBreakEvenQueries(qreal setupCostMilliseconds,
                                    qreal savingsPerQueryMilliseconds)
{
    if (setupCostMilliseconds <= 0.0) {
        return 0;
    }
    if (savingsPerQueryMilliseconds <= 0.0) {
        return std::numeric_limits<quint64>::max();
    }
    return static_cast<quint64>(std::ceil(
                setupCostMilliseconds / savingsPerQueryMilliseconds));
}

bool RunMG010BoundsCacheExperiment(QTextStream &output)
{
    const BoundsCacheComplexityCase cases[] = {
        {CanvasEntityType::Polyline, 3, "polyline_segments"},
        {CanvasEntityType::Polyline, 16, "polyline_segments"},
        {CanvasEntityType::Polyline, 64, "polyline_segments"},
        {CanvasEntityType::Polyline, 256, "polyline_segments"},
        {CanvasEntityType::Spline, 3, "spline_control_points"},
        {CanvasEntityType::Spline, 16, "spline_control_points"},
        {CanvasEntityType::Spline, 64, "spline_control_points"},
        {CanvasEntityType::GroupContainer, 3, "group_children"},
        {CanvasEntityType::GroupContainer, 16, "group_children"},
        {CanvasEntityType::GroupContainer, 64, "group_children"}
    };
    const int entityCount = 5000;
    const int candidatePercent = 1;
    const int caseCount = sizeof(cases) / sizeof(cases[0]);
    int completedCaseCount = 0;

    output << "# MG010_BOUNDS_CACHE_MATRIX,entity_count=5000"
              ",entity_types=Polyline|Spline|GroupContainer"
              ",complexity=prior_MG010_staircase"
              ",requested_candidate_percent=1"
              ",cache=QHash<ItemId,QRectF>_benchmark_model_only"
              ",query_comparison=live_TryGetGeometryBounds_vs_cached_read"
              ",mutation=CanvasDocument_TranslateItems_with_optional_cache_refresh"
              ",scope=geometry_bounds_only_excludes_hit_tolerance_and_path_build"
              ",sample_order=alternating_A_B\n";

    for (int caseIndex = 0; caseIndex < caseCount; ++caseIndex) {
        const BoundsCacheComplexityCase &complexityCase = cases[caseIndex];
        const QString complexityLabel = QStringLiteral("%1_%2")
                .arg(QString::fromLatin1(complexityCase.dimension))
                .arg(complexityCase.complexity);
        CanvasDocument document;
        QString sceneFailureReason;
        if (!PopulateBoundsComplexityDocument(
                    &document, complexityCase.entityType, entityCount,
                    candidatePercent, complexityCase.complexity,
                    &sceneFailureReason)) {
            output << "ERROR,bounds_cache_scene_build_failed," << complexityLabel
                   << ',' << sceneFailureReason << '\n';
            return false;
        }

        const QVector<ItemId> ids = document.GetItemIds();
        QHash<ItemId, QRectF> boundsCache;
        bool cacheBuildValid = true;
        quint64 builtBoundsCount = 0;
        const BenchmarkResult cacheBuildResult = Measure(
                    QStringLiteral("MG010_BoundsCacheBuild"), entityCount,
                    BoundsEntityCurvePercentage(complexityCase.entityType),
                    complexityLabel + QStringLiteral("_full_snapshot"), 1,
                    [&]() {
            QHash<ItemId, QRectF> candidateCache;
            if (!BuildBoundsCache(document, ids, &candidateCache)) {
                cacheBuildValid = false;
                return;
            }
            builtBoundsCount = static_cast<quint64>(candidateCache.size());
        });
        if (!cacheBuildValid || builtBoundsCount != entityCount
                || !BuildBoundsCache(document, ids, &boundsCache)
                || !VerifyBoundsCacheMatchesDocument(document, ids,
                                                     boundsCache)) {
            output << "ERROR,bounds_cache_build_failed," << complexityLabel
                   << ",built=" << builtBoundsCount
                   << ",expected=" << entityCount << '\n';
            return false;
        }
        PrintResult(output, cacheBuildResult);

        int recomputedCandidateCount = 0;
        int cachedCandidateCount = 0;
        if (!CountBoundsCandidates(document, ids, nullptr,
                                   kInteractionProbeCenter,
                                   &recomputedCandidateCount)
                || !CountBoundsCandidates(document, ids, &boundsCache,
                                          kInteractionProbeCenter,
                                          &cachedCandidateCount)
                || recomputedCandidateCount != cachedCandidateCount) {
            output << "ERROR,bounds_cache_initial_candidate_parity_failed,"
                   << complexityLabel << ",recomputed="
                   << recomputedCandidateCount << ",cached="
                   << cachedCandidateCount << '\n';
            return false;
        }

        output << "# MG010_BOUNDS_CACHE_CASE,entity_type="
               << CanvasEntityTypeName(complexityCase.entityType)
               << ",dimension=" << complexityCase.dimension
               << ",complexity=" << complexityCase.complexity
               << ",entities=" << entityCount
               << ",requested_candidate_percent=" << candidatePercent
               << ",candidate_count=" << recomputedCandidateCount
               << ",effective_candidate_percent="
               << QString::number(
                      100.0 * recomputedCandidateCount / entityCount, 'f', 3)
               << ",bounds_cache_entries=" << boundsCache.size()
               << ",mutation_target=first_entity\n";
        output.flush();

        BoundsCacheQuerySamples querySamples;
        if (!MeasureBoundsCacheQueries(
                    output, document, ids, boundsCache,
                    kInteractionProbeCenter, recomputedCandidateCount,
                    complexityLabel,
                    BoundsEntityCurvePercentage(complexityCase.entityType),
                    &querySamples)) {
            return false;
        }

        BoundsCacheMutationSamples mutationSamples;
        if (!MeasureBoundsCacheMutation(
                    output, complexityCase.entityType,
                    complexityCase.complexity,
                    entityCount, candidatePercent,
                    BoundsEntityCurvePercentage(complexityCase.entityType),
                    complexityLabel, &mutationSamples)) {
            return false;
        }

        const BenchmarkResult sortedCacheBuild = cacheBuildResult;
        const BenchmarkResult sortedLiveQuery = MakeBoundsCacheResult(
                    QString(), entityCount,
                    BoundsEntityCurvePercentage(complexityCase.entityType),
                    QString(), 1, querySamples.recomputeMilliseconds);
        const BenchmarkResult sortedCachedQuery = MakeBoundsCacheResult(
                    QString(), entityCount,
                    BoundsEntityCurvePercentage(complexityCase.entityType),
                    QString(), 1, querySamples.cachedMilliseconds);
        const BenchmarkResult sortedMutationOnly = MakeBoundsCacheResult(
                    QString(), entityCount,
                    BoundsEntityCurvePercentage(complexityCase.entityType),
                    QString(), 1, mutationSamples.mutationOnlyMilliseconds);
        const BenchmarkResult sortedMutationAndRefresh = MakeBoundsCacheResult(
                    QString(), entityCount,
                    BoundsEntityCurvePercentage(complexityCase.entityType),
                    QString(), 1,
                    mutationSamples.mutationAndRefreshMilliseconds);
        const BenchmarkResult pairedQuerySavings = MakeBoundsCacheResult(
                    QString(), entityCount,
                    BoundsEntityCurvePercentage(complexityCase.entityType),
                    QString(), 1, querySamples.savingsMilliseconds);
        const BenchmarkResult pairedMutationRefreshOverhead =
                MakeBoundsCacheResult(
                    QString(), entityCount,
                    BoundsEntityCurvePercentage(complexityCase.entityType),
                    QString(), 1,
                    mutationSamples.refreshOverheadMilliseconds);
        const qreal savingsPerQueryMilliseconds =
                pairedQuerySavings.medianMilliseconds;
        const qreal cacheUpdateOverheadMilliseconds =
                pairedMutationRefreshOverhead.medianMilliseconds;
        const quint64 initialBreakEvenQueries = BoundsCacheBreakEvenQueries(
                    sortedCacheBuild.medianMilliseconds,
                    savingsPerQueryMilliseconds);
        const quint64 mutationBreakEvenQueries = BoundsCacheBreakEvenQueries(
                    cacheUpdateOverheadMilliseconds,
                    savingsPerQueryMilliseconds);

        output << "# MG010_BOUNDS_CACHE_ANALYSIS,case=" << complexityLabel
               << ",live_query_median_ms="
               << QString::number(sortedLiveQuery.medianMilliseconds, 'f', 6)
               << ",cached_query_median_ms="
               << QString::number(sortedCachedQuery.medianMilliseconds, 'f', 6)
               << ",paired_query_savings_median_ms="
               << QString::number(savingsPerQueryMilliseconds, 'f', 6)
               << ",paired_query_savings_p95_ms="
               << QString::number(pairedQuerySavings.p95Milliseconds, 'f', 6)
               << ",cache_build_median_ms="
               << QString::number(sortedCacheBuild.medianMilliseconds, 'f', 6)
               << ",paired_mutation_refresh_extra_median_ms="
               << QString::number(cacheUpdateOverheadMilliseconds, 'f', 6)
               << ",paired_mutation_refresh_extra_p95_ms="
               << QString::number(
                      pairedMutationRefreshOverhead.p95Milliseconds, 'f', 6)
               << ",initial_break_even_queries=";
        if (initialBreakEvenQueries == std::numeric_limits<quint64>::max()) {
            output << "not_reached";
        } else {
            output << initialBreakEvenQueries;
        }
        output << ",post_mutation_break_even_queries=";
        if (mutationBreakEvenQueries == std::numeric_limits<quint64>::max()) {
            output << "not_reached";
        } else {
            output << mutationBreakEvenQueries;
        }
        output << '\n';
        ++completedCaseCount;
    }

    output << "# MG010_BOUNDS_CACHE_SUMMARY,cases=" << completedCaseCount
           << ",entity_count=" << entityCount
           << ",requested_candidate_percent=" << candidatePercent
           << ",live_cached_candidate_parity=true"
           << ",post_translate_cache_refresh_parity=true"
           << ",production_cache_enabled=false"
           << ",hit_tolerance_and_style_invalidation_not_measured=true\n";
    output.flush();
    return completedCaseCount == caseCount;
}
#endif

void SendWindowSelection(CanvasView *view)
{
    const QPointF start(4.0, 4.0);
    const QPointF end(view->width() - 4.0, view->height() - 4.0);
    SendMouseEvent(view, QEvent::MouseButtonPress, start,
                   Qt::LeftButton, Qt::LeftButton);
    SendMouseEvent(view, QEvent::MouseMove, end,
                   Qt::NoButton, Qt::LeftButton);
    SendMouseEvent(view, QEvent::MouseButtonRelease, end,
                   Qt::LeftButton, Qt::NoButton);
}

class BenchmarkCanvasData : public CanvasData
{
public:
    BenchmarkCanvasData(int commandCount,
                        int imageCount,
                        const QString &imagePath)
        : m_CommandCount(commandCount)
        , m_ImageCount(imageCount)
        , m_ImagePath(imagePath)
    {
    }

    std::shared_ptr<CanvasData> Clone() override
    {
        return std::make_shared<BenchmarkCanvasData>(m_CommandCount,
                                                      m_ImageCount,
                                                      m_ImagePath);
    }

    QStringList CalculateToQStringList() override
    {
        QStringList commands;
        commands.reserve(m_ImageCount > 0 ? m_ImageCount : m_CommandCount);
        for (int index = 0; index < m_ImageCount; ++index) {
            commands.append(QStringLiteral("Image,%1,%2,%3,0,1,1,0")
                            .arg(m_ImagePath)
                            .arg(-100 + (index % 10) * 30)
                            .arg(-100 + (index / 10) * 30));
        }
        for (int index = 0; index < m_CommandCount; ++index) {
            const qreal x = -100.0 + (index % 25) * 8.0;
            commands.append(QStringLiteral("Line,%1,-100,%2,100")
                            .arg(x, 0, 'f', 2)
                            .arg(x + 2.0, 0, 'f', 2));
        }
        return commands;
    }

    void Update(CanvasEntity *entity) override
    {
        Q_UNUSED(entity)
    }

private:
    int m_CommandCount;
    int m_ImageCount;
    QString m_ImagePath;
};

bool MeasureCanvasRender(QTextStream &output,
                         int entityCount,
                         SceneMix mix)
{
    CanvasDocument document;
    if (!PopulateDocument(&document, entityCount, mix)) {
        return false;
    }

    CanvasView view;
    ConfigureView(&view, &document, true);
    QImage target(view.size(), QImage::Format_ARGB32_Premultiplied);
    QPainter painter(&target);

    const BenchmarkResult result = Measure(
                QStringLiteral("MG009_FullFrameRender"), entityCount,
                CurvePercentage(mix), QStringLiteral("decorations_on"),
                IterationsForEntityCount(entityCount), [&]() {
        RenderView(&view, &target, &painter);
    });
    PrintResult(output, result);

    const qreal devicePixelRatio = view.devicePixelRatioF();
    const QSize pixelSize(qMax(1, qRound(view.width() * devicePixelRatio)),
                          qMax(1, qRound(view.height() * devicePixelRatio)));
    const QImage layer(pixelSize, QImage::Format_ARGB32_Premultiplied);
    const qint64 totalLayerBytes = 3LL * layer.bytesPerLine() * layer.height();
    output << "# layer_buffer_estimate_bytes=" << totalLayerBytes
           << ",dpr=" << QString::number(devicePixelRatio, 'f', 2)
           << ",size=" << view.width() << 'x' << view.height() << '\n';
    return true;
}

bool MeasureGeometrySnap(QTextStream &output,
                         CanvasDocument *document,
                         int entityCount,
                         SceneMix mix)
{
    if (!document) {
        return false;
    }

    const CanvasVector query(1000000.0, 1000000.0);
    if (CanvasSnapper::FindSnap(document, query, 4.0, 10.0,
                                true, false).IsValid()) {
        return false;
    }
    const BenchmarkResult result = Measure(
                QStringLiteral("MG010_GeometrySnapMiss"), entityCount,
                CurvePercentage(mix), QStringLiteral("geometry_on_grid_off"),
                IterationsForEntityCount(entityCount), [&]() {
        CanvasSnapper::FindSnap(document, query, 4.0, 10.0, true, false);
    });
    PrintResult(output, result);
    return true;
}

bool MeasureClick(QTextStream &output,
                  CanvasDocument *document,
                  int entityCount,
                  SceneMix mix,
                  bool hit)
{
    if (!document) {
        return false;
    }

    CanvasView view;
    ConfigureView(&view, document, false);
    if (hit) {
        view.CenterOnWorld(kInteractionProbeCenter);
    }
    const QPointF query = hit
            ? view.MapFromWorld(kInteractionProbeCenter)
            : QPointF(8.0, 8.0);
    SendClick(&view, query);
    if (hit) {
        if (!document->IsItemSelected(2)) {
            output << "ERROR,click_hit_precondition_failed\n";
            return false;
        }
        document->ClearSelection();
    } else if (document->GetSelectedItemCount() != 0) {
        output << "ERROR,click_miss_precondition_changed_selection\n";
        return false;
    }

    const QString workload = hit ? QStringLiteral("synthetic_click_hit")
                                 : QStringLiteral("synthetic_click_miss");
    const BenchmarkResult result = Measure(
                hit ? QStringLiteral("MG010_ClickHitEvent")
                    : QStringLiteral("MG010_ClickMissEvent"),
                entityCount, CurvePercentage(mix), workload,
                IterationsForEntityCount(entityCount), [&]() {
        SendClick(&view, query);
    });
    PrintResult(output, result);
    return true;
}

bool MeasureWindowSelection(QTextStream &output,
                            CanvasDocument *document,
                            int entityCount,
                            SceneMix mix)
{
    if (!document) {
        return false;
    }

    CanvasView view;
    ConfigureView(&view, document, false);
    SendWindowSelection(&view);
    if (document->GetSelectedItemCount() == 0) {
        output << "ERROR,window_selection_precondition_selected_nothing\n";
        return false;
    }
    document->ClearSelection();

    const BenchmarkResult result = Measure(
                QStringLiteral("MG010_WindowSelection"), entityCount,
                CurvePercentage(mix), QStringLiteral("full_view_window"),
                IterationsForEntityCount(entityCount), [&]() {
        SendWindowSelection(&view);
    });
    PrintResult(output, result);
    return true;
}

bool MeasureCanvasData(QTextStream &output,
                       int commandCount,
                       int imageCount,
                       const QString &imagePath)
{
    CanvasDocument document;
    CanvasPoint point(CanvasVector(0.0, 0.0));
    point.SetCanvasData(std::make_shared<BenchmarkCanvasData>(
                            commandCount, imageCount, imagePath));
    if (document.AddEntity(point) == 0) {
        return false;
    }

    CanvasView view;
    ConfigureView(&view, &document, false);
    QImage target(view.size(), QImage::Format_ARGB32_Premultiplied);
    QPainter painter(&target);
    const int iterations = imageCount > 0
            ? qMax(1, 8 / imageCount)
            : qMax(1, 200 / qMax(1, commandCount));
    const QString workload = imageCount > 0
            ? QStringLiteral("image_commands_%1_256px_warm_file_cache")
              .arg(imageCount)
            : QStringLiteral("generated_line_commands_%1")
              .arg(commandCount);
    const BenchmarkResult result = Measure(
                QStringLiteral("MG011_CanvasDataRender"), 1, 0,
                workload, iterations, [&]() {
        RenderView(&view, &target, &painter);
    });
    PrintResult(output, result);
    return true;
}

bool CreateBenchmarkImage(const QString &path)
{
    QImage image(256, 256, QImage::Format_ARGB32_Premultiplied);
    image.fill(QColor(20, 160, 220, 220));
    QPainter painter(&image);
    painter.fillRect(QRect(32, 32, 192, 192), QColor(240, 150, 30, 220));
    painter.end();
    return image.save(path, "PNG");
}

} // namespace

int main(int argc, char *argv[])
{
    QApplication application(argc, argv);
    QLocale::setDefault(QLocale::c());

    QTextStream output(stdout);
    output << "# qt=" << QT_VERSION_STR
           << ",abi=" << QSysInfo::buildAbi()
           << ",cpu_arch=" << QSysInfo::currentCpuArchitecture()
           << ",ideal_threads=" << QThread::idealThreadCount()
#ifdef NDEBUG
           << ",build=release"
#else
           << ",build=debug"
#endif
           << '\n';
    output << "# view=" << kViewSize.width() << 'x' << kViewSize.height()
           << ",warmup_samples=" << kWarmupSamples
           << ",measured_samples=" << kMeasuredSamples
           << ",percentiles=median,p95_linear_interpolation"
           << '\n';
    output << "scenario,entities,curve_percent,workload,median_ms,p95_ms,iterations_per_sample\n";

#ifdef MINIGRAPH_ENABLE_PERF_PROFILING
    if (argc > 1
            && QString::fromLocal8Bit(argv[1])
                    == QStringLiteral("--mg010-bounds-cache")) {
        output << "# MG010_BOUNDS_CACHE_EXPERIMENT,scope=profiling_build_only"
                  ",runtime_bounds_filter_default=off"
                  ",production_cache_enabled=false"
                  ",entity_count=5000"
                  ",query_samples=31"
                  ",mutation_path=CanvasDocument::TranslateItems"
                  ",paired_order=alternating_same_process\n";
        if (!RunMG010BoundsCacheExperiment(output)) {
            output.flush();
            return 2;
        }
        return 0;
    }

    if (argc > 1
            && QString::fromLocal8Bit(argv[1])
                    == QStringLiteral("--mg010-complexity")) {
        output << "# MG010_COMPLEXITY_EXPERIMENT,scope=profiling_build_only"
                  ",runtime_bounds_filter_default=off"
                  ",entity_count=5000"
                  ",paired_order=alternating_same_process\n";
        if (!RunMG010ComplexityExperiment(output)) {
            output.flush();
            return 2;
        }
        return 0;
    }

    if (argc > 1
            && QString::fromLocal8Bit(argv[1])
                    == QStringLiteral("--mg010-experiment")) {
        output << "# MG010_EXPERIMENT,scope=profiling_build_only"
                  ",runtime_bounds_filter_default=off"
                  ",paired_order=alternating_same_process\n";
        if (!RunMG010Experiment(output)) {
            output.flush();
            return 2;
        }
        return 0;
    }

    if (argc > 1
            && QString::fromLocal8Bit(argv[1])
                    == QStringLiteral("--mg010-profile")) {
        const SceneMix profileMixes[] = {SceneMix::Simple,
                                         SceneMix::CurveHeavy};
        for (SceneMix mix : profileMixes) {
            CanvasDocument document;
            output << "# MG010_PROFILE_SCENE_BUILD_BEGIN,5000,"
                   << CurvePercentage(mix) << '\n';
            output.flush();
            if (!PopulateDocument(&document, 5000, mix)
                    || !PrepareInteractionProbe(&document)
                    || !MeasureClickProfile(output, &document, 5000, mix, true)
                    || !MeasureClickProfile(output, &document, 5000, mix, false)) {
                output << "ERROR,hit_test_profile_failed,5000\n";
                output.flush();
                return 2;
            }
        }
        output.flush();
        return 0;
    }
#endif

    const int entityCounts[] = {100, 1000, 5000};
    for (int entityCount : entityCounts) {
        if (!MeasureCanvasRender(output, entityCount, SceneMix::CurveHeavy)) {
            output << "ERROR,failed_to_build_render_scene," << entityCount << '\n';
            return 2;
        }
    }

    const SceneMix mixes[] = {SceneMix::Simple, SceneMix::CurveHeavy};
    for (SceneMix mix : mixes) {
        for (int entityCount : entityCounts) {
            CanvasDocument document;
            if (!PopulateDocument(&document, entityCount, mix)) {
                output << "ERROR,failed_to_build_interaction_scene," << entityCount << '\n';
                return 2;
            }
            if (!PrepareInteractionProbe(&document)) {
                output << "ERROR,failed_to_prepare_interaction_probe,"
                       << entityCount << '\n';
                return 2;
            }
            if (!MeasureClick(output, &document, entityCount, mix, true)
                    || !MeasureClick(output, &document, entityCount, mix, false)
                    || !MeasureWindowSelection(output, &document,
                                               entityCount, mix)
                    || !MeasureGeometrySnap(output, &document,
                                            entityCount, mix)) {
                output << "ERROR,interaction_measurement_failed," << entityCount << '\n';
                return 2;
            }
#ifdef MINIGRAPH_ENABLE_PERF_PROFILING
            if (entityCount == 5000
                    && (!MeasureClickProfile(output, &document, entityCount,
                                             mix, true)
                        || !MeasureClickProfile(output, &document, entityCount,
                                                mix, false))) {
                output << "ERROR,hit_test_profile_failed," << entityCount << '\n';
                return 2;
            }
#endif
        }
    }

    QTemporaryDir temporaryDirectory;
    if (!temporaryDirectory.isValid()) {
        output << "ERROR,temporary_directory_unavailable\n";
        return 2;
    }
    const QString imagePath = temporaryDirectory.filePath(
                QStringLiteral("benchmark-image.png"));
    if (!CreateBenchmarkImage(imagePath)) {
        output << "ERROR,failed_to_create_benchmark_image\n";
        return 2;
    }

    const int commandCounts[] = {1, 10, 50};
    for (int commandCount : commandCounts) {
        if (!MeasureCanvasData(output, commandCount, 0, imagePath)) {
            output << "ERROR,canvas_data_command_measurement_failed,"
                   << commandCount << '\n';
            return 2;
        }
    }
    const int imageCounts[] = {1, 8};
    for (int imageCount : imageCounts) {
        if (!MeasureCanvasData(output, 0, imageCount, imagePath)) {
            output << "ERROR,canvas_data_image_measurement_failed,"
                   << imageCount << '\n';
            return 2;
        }
    }

    output.flush();
    return 0;
}
