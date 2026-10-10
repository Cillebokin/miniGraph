#include <QApplication>
#include <QColor>
#include <QElapsedTimer>
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

    BoundsModeSamples()
        : pathCollectionCalls(0)
        , itemIdVisits(0)
        , boundsChecks(0)
        , boundsRejects(0)
        , hitTestCalls(0)
    {
    }
};

enum class BoundsFilterWorkload
{
    SparseHit,
    Miss,
    DenseHit
};

QString BoundsFilterWorkloadName(BoundsFilterWorkload workload)
{
    switch (workload) {
    case BoundsFilterWorkload::SparseHit:
        return QStringLiteral("sparse_hit");
    case BoundsFilterWorkload::Miss:
        return QStringLiteral("miss");
    case BoundsFilterWorkload::DenseHit:
        return QStringLiteral("dense_overlap_hit");
    }
    return QStringLiteral("unknown");
}

bool MeasureBoundsFilterAba(QTextStream &output,
                            CanvasDocument *document,
                            int entityCount,
                            SceneMix mix,
                            BoundsFilterWorkload workloadType)
{
    if (!document) return false;

    const bool hit = workloadType != BoundsFilterWorkload::Miss;
    const bool dense = workloadType == BoundsFilterWorkload::DenseHit;
    const QString workload = BoundsFilterWorkloadName(workloadType);
    CanvasView view;
    ConfigureView(&view, document, false);
    view.setUpdatesEnabled(false);
    const CanvasVector worldQuery = hit
            ? kInteractionProbeCenter
            : CanvasVector(1000000.0, 1000000.0);
    view.CenterOnWorld(worldQuery);
    const QPointF viewportQuery = view.MapFromWorld(worldQuery);

    view.SetHitTestBoundsFilterEnabledForProfiling(false);
    document->ClearSelection();
    SendClick(&view, viewportQuery);
    const QVector<ItemId> expectedSelection = document->GetSelectedItemIds();
    if ((hit && expectedSelection.size() != 1)
            || (!hit && !expectedSelection.isEmpty())) {
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
            const quint64 expectedPathsPerHitTest = dense
                    ? static_cast<quint64>(entityCount)
                    : static_cast<quint64>(hit ? 1 : 0);
            const quint64 expectedPathCollectionCalls = mode == 0
                    ? static_cast<quint64>(entityCount) * 2
                    : expectedPathsPerHitTest * 2;
            const quint64 expectedBoundsChecks = mode == 0
                    ? 0 : static_cast<quint64>(entityCount) * 2;
            const quint64 expectedBoundsRejects = mode == 0
                    ? 0 : static_cast<quint64>(dense
                                               ? 0
                                               : entityCount
                                                 - (hit ? 1 : 0)) * 2;
            if (profile.hitTestCallCount != 2
                    || profile.itemIdVisitCount
                            != static_cast<quint64>(entityCount) * 2
                    || profile.pathCollectionCallCount
                            != expectedPathCollectionCalls
                    || profile.boundsCheckCount != expectedBoundsChecks
                    || profile.boundsRejectCount != expectedBoundsRejects) {
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
        result.scenario = QStringLiteral("MG010_BoundsFilterAba");
        result.entityCount = entityCount;
        result.curvePercent = CurvePercentage(mix);
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
               << CurvePercentage(mix) << ',' << workload
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
