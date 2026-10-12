#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <psapi.h>
#endif

#include <QApplication>
#include <QByteArray>
#include <QColor>
#include <QElapsedTimer>
#include <QHash>
#include <QImage>
#include <QLocale>
#include <QMouseEvent>
#include <QPainter>
#include <QProcess>
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

struct ProductionDisplayCacheCapacitySample
{
    qint64 privateUsageBeforeBytes = -1;
    qint64 privateUsageAfterBytes = -1;
    quint64 cacheEntryCount = 0;
    quint64 cacheHashCapacity = 0;
};

struct ProductionDisplayCacheTimingSummary
{
    qreal coldCacheMedianMilliseconds = 0.0;
    qreal coldCacheP95Milliseconds = 0.0;
    qreal coldLiveMedianMilliseconds = 0.0;
    qreal coldLiveP95Milliseconds = 0.0;
    qreal warmCacheMedianMilliseconds = 0.0;
    qreal warmCacheP95Milliseconds = 0.0;
    qreal warmLiveMedianMilliseconds = 0.0;
    qreal warmLiveP95Milliseconds = 0.0;
    qreal refreshCacheMedianMilliseconds = 0.0;
    qreal refreshCacheP95Milliseconds = 0.0;
    qreal refreshLiveMedianMilliseconds = 0.0;
    qreal refreshLiveP95Milliseconds = 0.0;
};

int IterationsForEntityCount(int entityCount);
int CurvePercentage(SceneMix mix);
void ConfigureView(CanvasView *view,
                   CanvasDocument *document,
                   bool drawCanvasDecorations);
void SendClick(CanvasView *view, const QPointF &position);
#ifdef MINIGRAPH_ENABLE_PERF_PROFILING
bool BuildBoundsCache(const CanvasDocument &document,
                      const QVector<ItemId> &ids,
                      QHash<ItemId, QRectF> *boundsCache);
bool VerifyBoundsCacheMatchesDocument(
        const CanvasDocument &document,
        const QVector<ItemId> &ids,
        const QHash<ItemId, QRectF> &boundsCache);
bool CountBoundsCandidates(const CanvasDocument &document,
                           const QVector<ItemId> &ids,
                           const QHash<ItemId, QRectF> *boundsCache,
                           const CanvasVector &query,
                           int *candidateCount);
#endif

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

#ifdef MINIGRAPH_ENABLE_PERF_PROFILING
bool ReadCurrentProcessPrivateUsageBytes(qint64 *bytes)
{
    if (!bytes) {
        return false;
    }
#ifdef _WIN32
    PROCESS_MEMORY_COUNTERS_EX counters = {};
    counters.cb = sizeof(counters);
    if (!GetProcessMemoryInfo(GetCurrentProcess(),
                              reinterpret_cast<PROCESS_MEMORY_COUNTERS *>(
                                  &counters),
                              sizeof(counters))) {
        return false;
    }
    *bytes = static_cast<qint64>(counters.PrivateUsage);
    return true;
#else
    return false;
#endif
}
#endif

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

    const QVector<ItemId> ids = document.GetItemIds();
    QHash<ItemId, QRectF> geometryBoundsCache;
    if (!BuildBoundsCache(document, ids, &geometryBoundsCache)
            || !VerifyBoundsCacheMatchesDocument(
                document, ids, geometryBoundsCache)) {
        output << "ERROR,bounds_parity_geometry_cache_build_failed\n";
        return false;
    }

    CanvasView view;
    ConfigureView(&view, &document, false);
    view.setUpdatesEnabled(false);
    view.SetHitTestMaximumDisplaySizeCacheBypassedForProfiling(true);
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
        QVector<ItemId> outcomes[3];
        for (int mode = 0; mode < 3; ++mode) {
            document.ClearSelection();
            view.SetHitTestBoundsFilterEnabledForProfiling(mode != 0);
            if (mode == 2) {
                view.SetHitTestGeometryBoundsCacheForProfiling(
                            geometryBoundsCache);
            } else {
                view.ClearHitTestGeometryBoundsCacheForProfiling();
            }
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
        if (outcomes[0] != outcomes[1] || outcomes[0] != outcomes[2]) {
            output << "ERROR,bounds_parity_selection_mismatch,"
                   << target.name << ",mode=live_or_cached\n";
            return false;
        }
        output << "# MG010_BOUNDS_PARITY," << target.name
               << ",expected=" << (target.expectedHit ? "hit" : "miss")
               << ",baseline_item="
               << (outcomes[0].isEmpty() ? 0 : outcomes[0].first())
               << ",live_filtered_item="
               << (outcomes[1].isEmpty() ? 0 : outcomes[1].first())
               << ",cached_filtered_item="
               << (outcomes[2].isEmpty() ? 0 : outcomes[2].first())
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
           << ",filter_off_live_cached_selection_parity=true\n";
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
    quint64 geometryBoundsCacheLookups;
    quint64 geometryBoundsCacheMisses;
    quint64 hitTestCalls;
    quint64 selectionPathCount;
    quint64 selectionPathPointCount;
    quint64 distanceSegmentCount;

    BoundsModeSamples()
        : pathCollectionCalls(0)
        , itemIdVisits(0)
        , boundsChecks(0)
        , boundsRejects(0)
        , geometryBoundsCacheLookups(0)
        , geometryBoundsCacheMisses(0)
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
                            int complexitySelectionPathsPerEntity = -1,
                            bool includeGeometryBoundsCache = false)
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
    view.SetHitTestMaximumDisplaySizeCacheBypassedForProfiling(true);
    const CanvasVector worldQuery = queryAtProbe
            ? kInteractionProbeCenter
            : CanvasVector(1000000.0, 1000000.0);
    view.CenterOnWorld(worldQuery);
    const QPointF viewportQuery = view.MapFromWorld(worldQuery);

    QHash<ItemId, QRectF> geometryBoundsCache;
    if (includeGeometryBoundsCache) {
        const QVector<ItemId> ids = document->GetItemIds();
        int liveCandidateCount = 0;
        int cachedCandidateCount = 0;
        if (ids.size() != entityCount
                || !BuildBoundsCache(*document, ids, &geometryBoundsCache)
                || !VerifyBoundsCacheMatchesDocument(
                    *document, ids, geometryBoundsCache)
                || !CountBoundsCandidates(*document, ids, nullptr,
                                          worldQuery, &liveCandidateCount)
                || !CountBoundsCandidates(*document, ids,
                                          &geometryBoundsCache, worldQuery,
                                          &cachedCandidateCount)
                || cachedCandidateCount != liveCandidateCount) {
            output << "ERROR,bounds_cache_filter_raw_candidate_parity_failed,"
                   << entityCount << ',' << workload
                   << ",live_raw_candidates=" << liveCandidateCount
                   << ",cached_raw_candidates=" << cachedCandidateCount
                   << ",expected_conservative_candidates="
                   << expectedBoundsCandidates << '\n';
            return false;
        }
    }

    view.SetHitTestBoundsFilterEnabledForProfiling(false);
    view.ClearHitTestGeometryBoundsCacheForProfiling();
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

    const int modeCount = includeGeometryBoundsCache ? 3 : 2;
    BoundsModeSamples modes[3];
    for (int sample = 0; sample < kWarmupSamples; ++sample) {
        const int firstMode = sample % modeCount;
        for (int order = 0; order < modeCount; ++order) {
            const int mode = (firstMode + order) % modeCount;
            document->ClearSelection();
            view.SetHitTestBoundsFilterEnabledForProfiling(mode != 0);
            if (mode == 2) {
                view.SetHitTestGeometryBoundsCacheForProfiling(
                            geometryBoundsCache);
            } else {
                view.ClearHitTestGeometryBoundsCacheForProfiling();
            }
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
        const int firstMode = sample % modeCount;
        QVector<ItemId> pairedOutcome[3];
        for (int order = 0; order < modeCount; ++order) {
            const int mode = (firstMode + order) % modeCount;
            document->ClearSelection();
            view.SetHitTestBoundsFilterEnabledForProfiling(mode != 0);
            if (mode == 2) {
                view.SetHitTestGeometryBoundsCacheForProfiling(
                            geometryBoundsCache);
            } else {
                view.ClearHitTestGeometryBoundsCacheForProfiling();
            }
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
            const quint64 expectedCacheLookups = mode == 2
                    ? static_cast<quint64>(entityCount) * 2 : 0;
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
                    || profile.geometryBoundsCacheLookupCount
                            != expectedCacheLookups
                    || profile.geometryBoundsCacheMissCount != 0
                    || !entityTypeProfileMatches) {
                output << "ERROR,bounds_ab_profile_invariant_failed,"
                       << entityCount << ',' << workload << ',' << mode
                       << ',' << sample << ','
                       << profile.hitTestCallCount << ','
                       << profile.itemIdVisitCount << ','
                       << profile.pathCollectionCallCount << ','
                       << profile.boundsCheckCount << ','
                       << profile.boundsRejectCount << ','
                       << profile.geometryBoundsCacheLookupCount << ','
                       << profile.geometryBoundsCacheMissCount << '\n';
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
            samples.geometryBoundsCacheLookups +=
                    profile.geometryBoundsCacheLookupCount;
            samples.geometryBoundsCacheMisses +=
                    profile.geometryBoundsCacheMissCount;
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
        for (int mode = 1; mode < modeCount; ++mode) {
            if (pairedOutcome[0] != pairedOutcome[mode]) {
                output << "ERROR,bounds_ab_paired_selection_mismatch,"
                       << entityCount << ',' << workload << ','
                       << sample << ",baseline_mode=0,compared_mode="
                       << mode << '\n';
                return false;
            }
        }
    }

    for (int mode = 0; mode < modeCount; ++mode) {
        BoundsModeSamples &samples = modes[mode];
        BenchmarkResult result;
        result.scenario = entityTypeScoped
                ? (complexityLabel.isEmpty()
                   ? QStringLiteral("MG010_BoundsFilterEntityTypeAba")
                   : QStringLiteral("MG010_BoundsFilterComplexityAba"))
                : QStringLiteral("MG010_BoundsFilterAba");
        result.entityCount = entityCount;
        result.curvePercent = curvePercent;
        const QString modeName = mode == 0
                ? QStringLiteral("filter_off")
                : (mode == 1
                   ? (includeGeometryBoundsCache
                      ? QStringLiteral("filter_live_bounds")
                      : QStringLiteral("filter_on"))
                             : QStringLiteral("filter_cached_geometry_bounds"));
        result.workload = workload + QLatin1Char('_') + modeName;
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
               << ",mode=" << modeName
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
               << ",geometry_bounds_cache_lookups_per_hit_test="
               << QString::number(static_cast<qreal>(
                                      samples.geometryBoundsCacheLookups)
                                  / samples.hitTestCalls, 'f', 3)
               << ",geometry_bounds_cache_misses_per_hit_test="
               << QString::number(static_cast<qreal>(
                                      samples.geometryBoundsCacheMisses)
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
               << ",selection_parity=true"
               << ",cached_geometry_only="
               << (mode == 2 ? "true" : "false")
               << ",maximum_display_size_recomputed=true"
               << ",hit_tolerance_unchanged=true\n";
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

struct HitDisplaySizeModeSamples
{
    QVector<qreal> eventMilliseconds;
    QVector<qreal> boundsCheckMilliseconds;
    QVector<qreal> geometryAccessMilliseconds;
    QVector<qreal> displaySizeScanMilliseconds;
    QVector<qreal> displaySizeCacheLookupMilliseconds;
    QVector<qreal> boundsExpansionMilliseconds;
    QVector<qreal> displaySizeNodesPerHitTest;
    QVector<qreal> displaySizeScanCallsPerHitTest;
    QVector<qreal> displaySizeCacheHitsPerHitTest;
};

bool SetBenchmarkHitDisplaySize(CanvasEntity *entity, qreal displaySize)
{
    if (!entity || !std::isfinite(static_cast<double>(displaySize))
            || displaySize < 0.0) {
        return false;
    }

    StruCanvasStyle style = entity->GetStyle();
    if (entity->GetType() == CanvasEntityType::Point) {
        style.pointSize = displaySize;
    } else {
        style.strokeWidth = displaySize;
    }
    return entity->SetStyle(style);
}

bool VerifyMaximumDisplaySizeCacheMutationParity(QTextStream &output)
{
    const CanvasVector query = kInteractionProbeCenter;
    CanvasPoint point(CanvasVector(query.GetX() + 12.0, query.GetY()));
    StruCanvasStyle wideStyle = point.GetStyle();
    wideStyle.pointSize = 32.0;
    if (!point.SetStyle(wideStyle)) {
        output << "ERROR,display_size_cache_mutation_setup_failed\n";
        return false;
    }

    CanvasDocument document;
    const ItemId id = document.AddEntity(point);
    CanvasView view;
    ConfigureView(&view, &document, false);
    view.setUpdatesEnabled(false);
    view.CenterOnWorld(query);
    const QPointF viewportQuery = view.MapFromWorld(query);

    QHash<ItemId, QRectF> boundsCache;
    QHash<ItemId, qreal> displaySizeCache;
    quint64 visitedEntityCount = 0;
    if (id == 0
            || !BuildBoundsCache(document, document.GetItemIds(),
                                 &boundsCache)
            || !view.BuildHitTestMaximumDisplaySizeCacheForProfiling(
                &displaySizeCache, &visitedEntityCount)
            || visitedEntityCount != 1) {
        output << "ERROR,display_size_cache_mutation_initial_cache_failed\n";
        return false;
    }

    view.SetHitTestBoundsFilterEnabledForProfiling(false);
    view.ClearHitTestGeometryBoundsCacheForProfiling();
    view.ClearHitTestMaximumDisplaySizeCacheForProfiling();
    SendClick(&view, viewportQuery);
    const QVector<ItemId> wideStyleUnfilteredSelection =
            document.GetSelectedItemIds();
    if (wideStyleUnfilteredSelection.size() != 1
            || wideStyleUnfilteredSelection.first() != id) {
        output << "ERROR,display_size_cache_wide_style_unfiltered_precondition_failed\n";
        return false;
    }

    document.ClearSelection();
    view.SetHitTestGeometryBoundsCacheForProfiling(boundsCache);
    view.SetHitTestMaximumDisplaySizeCacheForProfiling(displaySizeCache);
    view.SetHitTestBoundsFilterEnabledForProfiling(true);
    SendClick(&view, viewportQuery);
    if (document.GetSelectedItemIds() != wideStyleUnfilteredSelection) {
        output << "ERROR,display_size_cache_wide_style_cached_parity_failed\n";
        return false;
    }

    const CanvasEntity *current = document.GetEntity(id);
    QSharedPointer<CanvasEntity> narrowed = current ? current->Clone()
                                                    : QSharedPointer<CanvasEntity>();
    if (!narrowed) {
        output << "ERROR,display_size_cache_mutation_clone_failed\n";
        return false;
    }
    StruCanvasStyle narrowStyle = narrowed->GetStyle();
    narrowStyle.pointSize = 2.0;
    const bool narrowStyleSet = narrowed->SetStyle(narrowStyle);
    const bool entityReplaced = narrowStyleSet
            && document.ReplaceEntity(id, *narrowed);
    const bool geometryCacheRebuilt = entityReplaced
            && BuildBoundsCache(document, document.GetItemIds(), &boundsCache);
    const bool displayCacheRebuilt = geometryCacheRebuilt
            && view.BuildHitTestMaximumDisplaySizeCacheForProfiling(
                &displaySizeCache, &visitedEntityCount);
    if (!narrowStyleSet || !entityReplaced || !geometryCacheRebuilt
            || !displayCacheRebuilt || visitedEntityCount != 1) {
        output << "ERROR,display_size_cache_mutation_refresh_failed"
               << ",style_set=" << narrowStyleSet
               << ",entity_replaced=" << entityReplaced
               << ",geometry_cache=" << geometryCacheRebuilt
               << ",display_cache=" << displayCacheRebuilt
               << ",visited=" << visitedEntityCount << '\n';
        return false;
    }

    view.SetHitTestBoundsFilterEnabledForProfiling(false);
    view.ClearHitTestGeometryBoundsCacheForProfiling();
    view.ClearHitTestMaximumDisplaySizeCacheForProfiling();
    document.ClearSelection();
    SendClick(&view, viewportQuery);
    const QVector<ItemId> narrowStyleUnfilteredSelection =
            document.GetSelectedItemIds();
    if (!narrowStyleUnfilteredSelection.isEmpty()) {
        output << "ERROR,display_size_cache_narrow_style_unfiltered_precondition_failed\n";
        return false;
    }

    view.SetHitTestGeometryBoundsCacheForProfiling(boundsCache);
    view.SetHitTestMaximumDisplaySizeCacheForProfiling(displaySizeCache);
    view.SetHitTestBoundsFilterEnabledForProfiling(true);
    document.ClearSelection();
    SendClick(&view, viewportQuery);
    if (document.GetSelectedItemIds() != narrowStyleUnfilteredSelection) {
        output << "ERROR,display_size_cache_narrow_style_cached_parity_failed\n";
        return false;
    }

    output << "# MG010_DISPLAY_SIZE_CACHE_MUTATION_PARITY"
              ",wide_style_near_miss_accepted=true"
              ",replaced_with_narrow_style_and_cache_rebuilt=true"
              ",narrow_style_near_miss_rejected=true"
              ",cached_vs_uncached_selection_parity=true\n";
    return true;
}

bool MeasureHitDisplaySizeCacheMaintenance(
        QTextStream &output,
        CanvasDocument *document,
        CanvasView *view,
        int entityCount,
        int curvePercent,
        const QString &caseName,
        QHash<ItemId, qreal> *displaySizeCache,
        BenchmarkResult *replaceOnlyResult,
        BenchmarkResult *replaceAndRebuildResult)
{
    if (!document || !view || !displaySizeCache
            || !replaceOnlyResult || !replaceAndRebuildResult) {
        return false;
    }

    const QVector<ItemId> ids = document->GetItemIds();
    const ItemId id = ids.isEmpty() ? 0 : ids.first();
    const CanvasEntity *source = document->GetEntity(id);
    QSharedPointer<CanvasEntity> firstStyle = source ? source->Clone()
                                                     : QSharedPointer<CanvasEntity>();
    QSharedPointer<CanvasEntity> secondStyle = source ? source->Clone()
                                                      : QSharedPointer<CanvasEntity>();
    if (id == 0 || !firstStyle || !secondStyle
            || !SetBenchmarkHitDisplaySize(firstStyle.data(), 24.0)
            || !SetBenchmarkHitDisplaySize(secondStyle.data(), 40.0)) {
        output << "ERROR,display_size_cache_mutation_variants_failed," << caseName
               << '\n';
        return false;
    }

    bool mutationValid = true;
    int replaceOnlyIndex = 0;
    *replaceOnlyResult = Measure(
                QStringLiteral("MG010_MaxDisplayCacheMutation"), entityCount,
                curvePercent, caseName + QStringLiteral("_replace_only"), 1,
                [&]() {
        const CanvasEntity &replacement = replaceOnlyIndex++ % 2 == 0
                ? *firstStyle : *secondStyle;
        if (!document->ReplaceEntity(id, replacement)) {
            mutationValid = false;
        }
    });
    if (!mutationValid) {
        output << "ERROR,display_size_cache_replace_only_failed," << caseName
               << '\n';
        return false;
    }
    PrintResult(output, *replaceOnlyResult);

    int refreshIndex = 0;
    *replaceAndRebuildResult = Measure(
                QStringLiteral("MG010_MaxDisplayCacheMutation"), entityCount,
                curvePercent,
                caseName + QStringLiteral("_replace_plus_full_cache_rebuild"),
                1, [&]() {
        const CanvasEntity &replacement = refreshIndex++ % 2 == 0
                ? *firstStyle : *secondStyle;
        if (!document->ReplaceEntity(id, replacement)) {
            mutationValid = false;
            return;
        }

        QHash<ItemId, qreal> refreshedCache;
        if (!view->BuildHitTestMaximumDisplaySizeCacheForProfiling(
                    &refreshedCache)
                || refreshedCache.size() != entityCount) {
            mutationValid = false;
            return;
        }
        displaySizeCache->swap(refreshedCache);
    });
    if (!mutationValid) {
        output << "ERROR,display_size_cache_replace_refresh_failed," << caseName
               << '\n';
        return false;
    }
    PrintResult(output, *replaceAndRebuildResult);

    return view->BuildHitTestMaximumDisplaySizeCacheForProfiling(
                displaySizeCache)
            && displaySizeCache->size() == entityCount;
}

bool MeasureHitDisplaySizeCacheCase(
        QTextStream &output,
        CanvasDocument *document,
        int entityCount,
        int curvePercent,
        const QString &caseName,
        const CanvasVector &worldQuery,
        bool expectedHit,
        BenchmarkResult modeResults[4],
        BenchmarkResult *cacheBuildResult,
        BenchmarkResult *replaceOnlyResult,
        BenchmarkResult *replaceAndRebuildResult)
{
    if (!document || document->GetItemCount() != entityCount
            || !cacheBuildResult || !replaceOnlyResult
            || !replaceAndRebuildResult) {
        return false;
    }

    CanvasView view;
    ConfigureView(&view, document, false);
    view.setUpdatesEnabled(false);
    view.SetHitTestMaximumDisplaySizeCacheBypassedForProfiling(true);
    view.CenterOnWorld(worldQuery);
    const QPointF viewportQuery = view.MapFromWorld(worldQuery);

    const QVector<ItemId> ids = document->GetItemIds();
    QHash<ItemId, QRectF> geometryBoundsCache;
    if (!BuildBoundsCache(*document, ids, &geometryBoundsCache)
            || !VerifyBoundsCacheMatchesDocument(
                *document, ids, geometryBoundsCache)) {
        output << "ERROR,display_size_cache_geometry_snapshot_failed," << caseName
               << '\n';
        return false;
    }

    QHash<ItemId, qreal> displaySizeCache;
    quint64 displaySizeTreeNodeCount = 0;
    bool cacheBuildValid = true;
    *cacheBuildResult = Measure(
                QStringLiteral("MG010_MaxDisplayCacheBuild"), entityCount,
                curvePercent, caseName + QStringLiteral("_full_snapshot"), 1,
        [&]() {
        QHash<ItemId, qreal> candidateCache;
        if (!view.BuildHitTestMaximumDisplaySizeCacheForProfiling(
                    &candidateCache)
                || candidateCache.size() != entityCount) {
            cacheBuildValid = false;
            return;
        }
        displaySizeCache.swap(candidateCache);
    });
    if (!cacheBuildValid || displaySizeCache.size() != entityCount
            || displaySizeCache.isEmpty()) {
        output << "ERROR,display_size_cache_build_failed," << caseName << '\n';
        return false;
    }
    PrintResult(output, *cacheBuildResult);

    if (!view.BuildHitTestMaximumDisplaySizeCacheForProfiling(
                &displaySizeCache, &displaySizeTreeNodeCount)
            || displaySizeCache.size() != entityCount
            || displaySizeTreeNodeCount == 0) {
        output << "ERROR,display_size_cache_snapshot_parity_failed," << caseName
               << '\n';
        return false;
    }

    view.SetHitTestBoundsFilterEnabledForProfiling(false);
    view.ClearHitTestGeometryBoundsCacheForProfiling();
    view.ClearHitTestMaximumDisplaySizeCacheForProfiling();
    document->ClearSelection();
    SendClick(&view, viewportQuery);
    const QVector<ItemId> expectedSelection = document->GetSelectedItemIds();
    if (expectedHit != !expectedSelection.isEmpty()) {
        output << "ERROR,display_size_cache_scene_precondition_failed," << caseName
               << ",selected=" << expectedSelection.size() << '\n';
        return false;
    }

    const QString modeNames[] = {
        QStringLiteral("filter_off"),
        QStringLiteral("filter_live_geometry_live_style"),
        QStringLiteral("filter_cached_geometry_live_style"),
        QStringLiteral("filter_cached_geometry_cached_style")
    };
    HitDisplaySizeModeSamples samples[4];
    for (int sample = 0; sample < kWarmupSamples; ++sample) {
        const int firstMode = sample % 4;
        for (int order = 0; order < 4; ++order) {
            const int mode = (firstMode + order) % 4;
            document->ClearSelection();
            view.SetHitTestBoundsFilterEnabledForProfiling(mode != 0);
            if (mode >= 2) {
                view.SetHitTestGeometryBoundsCacheForProfiling(
                            geometryBoundsCache);
            } else {
                view.ClearHitTestGeometryBoundsCacheForProfiling();
            }
            if (mode == 3) {
                view.SetHitTestMaximumDisplaySizeCacheForProfiling(
                            displaySizeCache);
            } else {
                view.ClearHitTestMaximumDisplaySizeCacheForProfiling();
            }
            SendClick(&view, viewportQuery);
            if (document->GetSelectedItemIds() != expectedSelection) {
                output << "ERROR,display_size_cache_warmup_selection_mismatch,"
                       << caseName << ',' << mode << '\n';
                return false;
            }
        }
    }

    for (int sample = 0; sample < kMeasuredSamples; ++sample) {
        const int firstMode = sample % 4;
        for (int order = 0; order < 4; ++order) {
            const int mode = (firstMode + order) % 4;
            document->ClearSelection();
            view.SetHitTestBoundsFilterEnabledForProfiling(mode != 0);
            if (mode >= 2) {
                view.SetHitTestGeometryBoundsCacheForProfiling(
                            geometryBoundsCache);
            } else {
                view.ClearHitTestGeometryBoundsCacheForProfiling();
            }
            if (mode == 3) {
                view.SetHitTestMaximumDisplaySizeCacheForProfiling(
                            displaySizeCache);
            } else {
                view.ClearHitTestMaximumDisplaySizeCacheForProfiling();
            }
            view.ResetHitTestProfile();

            QElapsedTimer eventTimer;
            eventTimer.start();
            SendClick(&view, viewportQuery);
            samples[mode].eventMilliseconds.append(
                        eventTimer.nsecsElapsed() / 1000000.0);
            if (document->GetSelectedItemIds() != expectedSelection) {
                output << "ERROR,display_size_cache_selection_mismatch,"
                       << caseName << ',' << mode << ',' << sample << '\n';
                return false;
            }

            const CanvasView::CanvasHitTestProfile profile =
                    view.GetHitTestProfile();
            const quint64 expectedHitTestCount = 2;
            const quint64 expectedEntityVisits =
                    static_cast<quint64>(entityCount) * expectedHitTestCount;
            const quint64 expectedFiltered = mode == 0 ? 0
                    : expectedEntityVisits;
            const quint64 expectedGeometryCacheLookups = mode >= 2
                    ? expectedEntityVisits : 0;
            const quint64 expectedDisplayCacheLookups = mode == 3
                    ? expectedEntityVisits : 0;
            const quint64 expectedDisplaySizeScans =
                    mode == 1 || mode == 2 ? expectedEntityVisits : 0;
            const quint64 expectedDisplaySizeTreeVisits =
                    (mode == 1 || mode == 2)
                    ? displaySizeTreeNodeCount * expectedHitTestCount : 0;
            if (profile.hitTestCallCount != expectedHitTestCount
                    || profile.itemIdVisitCount != expectedEntityVisits
                    || profile.boundsCheckCount != expectedFiltered
                    || profile.geometryBoundsCacheLookupCount
                            != expectedGeometryCacheLookups
                    || profile.geometryBoundsCacheMissCount != 0
                    || profile.maximumDisplaySizeCacheLookupCount
                            != expectedDisplayCacheLookups
                    || profile.maximumDisplaySizeCacheMissCount != 0
                    || profile.maximumDisplaySizeCalculationCallCount
                            != expectedDisplaySizeScans
                    || (mode != 0
                        && profile.hitBoundsExpansionNanoseconds <= 0)) {
                output << "ERROR,display_size_cache_profile_invariant_failed,"
                       << caseName << ',' << mode << ',' << sample << ','
                       << profile.hitTestCallCount << ','
                       << profile.boundsCheckCount << ','
                       << profile.maximumDisplaySizeCacheLookupCount << ','
                       << profile.maximumDisplaySizeCalculationCallCount << '\n';
                return false;
            }

            const qreal hitTestCount =
                    static_cast<qreal>(profile.hitTestCallCount);
            samples[mode].boundsCheckMilliseconds.append(
                        profile.boundsCheckNanoseconds
                        / 1000000.0 / hitTestCount);
            samples[mode].geometryAccessMilliseconds.append(
                        profile.geometryBoundsAccessNanoseconds
                        / 1000000.0 / hitTestCount);
            samples[mode].displaySizeScanMilliseconds.append(
                        profile.maximumDisplaySizeCalculationNanoseconds
                        / 1000000.0 / hitTestCount);
            samples[mode].displaySizeCacheLookupMilliseconds.append(
                        profile.maximumDisplaySizeCacheLookupNanoseconds
                        / 1000000.0 / hitTestCount);
            samples[mode].boundsExpansionMilliseconds.append(
                        profile.hitBoundsExpansionNanoseconds
                        / 1000000.0 / hitTestCount);
            samples[mode].displaySizeNodesPerHitTest.append(
                        static_cast<qreal>(expectedDisplaySizeTreeVisits)
                        / hitTestCount);
            samples[mode].displaySizeScanCallsPerHitTest.append(
                        static_cast<qreal>(
                            profile.maximumDisplaySizeCalculationCallCount)
                        / hitTestCount);
            samples[mode].displaySizeCacheHitsPerHitTest.append(
                        static_cast<qreal>(
                            profile.maximumDisplaySizeCacheLookupCount
                            - profile.maximumDisplaySizeCacheMissCount)
                        / hitTestCount);
        }
    }

    for (int mode = 0; mode < 4; ++mode) {
        modeResults[mode] = MakeBoundsCacheResult(
                    QStringLiteral("MG010_MaxDisplaySizeCacheAba"),
                    entityCount, curvePercent,
                    caseName + QLatin1Char('_') + modeNames[mode], 1,
                    samples[mode].eventMilliseconds);
        PrintResult(output, modeResults[mode]);
        PrintProfileMetric(output, entityCount, curvePercent,
                           caseName + QLatin1Char('_') + modeNames[mode],
                           QStringLiteral("bounds_check_ms_per_hit_test"),
                           samples[mode].boundsCheckMilliseconds,
                           QStringLiteral("ms/hit_test"));
        PrintProfileMetric(output, entityCount, curvePercent,
                           caseName + QLatin1Char('_') + modeNames[mode],
                           QStringLiteral("geometry_bounds_access_ms_per_hit_test"),
                           samples[mode].geometryAccessMilliseconds,
                           QStringLiteral("ms/hit_test"));
        PrintProfileMetric(output, entityCount, curvePercent,
                           caseName + QLatin1Char('_') + modeNames[mode],
                           QStringLiteral("maximum_display_size_scan_ms_per_hit_test"),
                           samples[mode].displaySizeScanMilliseconds,
                           QStringLiteral("ms/hit_test"));
        PrintProfileMetric(output, entityCount, curvePercent,
                           caseName + QLatin1Char('_') + modeNames[mode],
                           QStringLiteral("maximum_display_size_cache_lookup_ms_per_hit_test"),
                           samples[mode].displaySizeCacheLookupMilliseconds,
                           QStringLiteral("ms/hit_test"));
        PrintProfileMetric(output, entityCount, curvePercent,
                           caseName + QLatin1Char('_') + modeNames[mode],
                           QStringLiteral("hit_bounds_expansion_ms_per_hit_test"),
                           samples[mode].boundsExpansionMilliseconds,
                           QStringLiteral("ms/hit_test"));
        PrintProfileMetric(output, entityCount, curvePercent,
                           caseName + QLatin1Char('_') + modeNames[mode],
                           QStringLiteral("maximum_display_size_nodes_per_hit_test"),
                           samples[mode].displaySizeNodesPerHitTest,
                           QStringLiteral("nodes/hit_test"));
        PrintProfileMetric(output, entityCount, curvePercent,
                           caseName + QLatin1Char('_') + modeNames[mode],
                           QStringLiteral("maximum_display_size_scans_per_hit_test"),
                           samples[mode].displaySizeScanCallsPerHitTest,
                           QStringLiteral("scans/hit_test"));
        PrintProfileMetric(output, entityCount, curvePercent,
                           caseName + QLatin1Char('_') + modeNames[mode],
                           QStringLiteral("maximum_display_size_cache_hits_per_hit_test"),
                           samples[mode].displaySizeCacheHitsPerHitTest,
                           QStringLiteral("hits/hit_test"));
    }

    if (!MeasureHitDisplaySizeCacheMaintenance(
                output, document, &view, entityCount, curvePercent, caseName,
                &displaySizeCache, replaceOnlyResult,
                replaceAndRebuildResult)) {
        return false;
    }

    const qreal querySavingsMilliseconds =
            modeResults[2].medianMilliseconds
            - modeResults[3].medianMilliseconds;
    const qreal rebuildOverheadMilliseconds =
            replaceAndRebuildResult->medianMilliseconds
            - replaceOnlyResult->medianMilliseconds;
    const quint64 buildBreakEvenQueries = BoundsCacheBreakEvenQueries(
                cacheBuildResult->medianMilliseconds,
                querySavingsMilliseconds);
    const quint64 refreshBreakEvenQueries = BoundsCacheBreakEvenQueries(
                rebuildOverheadMilliseconds, querySavingsMilliseconds);
    output << "# MG010_DISPLAY_SIZE_CACHE_ANALYSIS,case=" << caseName
           << ",cache_build_median_ms="
           << QString::number(cacheBuildResult->medianMilliseconds, 'f', 6)
           << ",live_style_query_median_ms="
           << QString::number(modeResults[2].medianMilliseconds, 'f', 6)
           << ",cached_style_query_median_ms="
           << QString::number(modeResults[3].medianMilliseconds, 'f', 6)
           << ",paired_query_savings_ms="
           << QString::number(querySavingsMilliseconds, 'f', 6)
           << ",replace_only_median_ms="
           << QString::number(replaceOnlyResult->medianMilliseconds, 'f', 6)
           << ",replace_plus_full_rebuild_median_ms="
           << QString::number(replaceAndRebuildResult->medianMilliseconds,
                              'f', 6)
           << ",full_rebuild_incremental_cost_ms="
           << QString::number(rebuildOverheadMilliseconds, 'f', 6)
           << ",build_break_even_queries=";
    if (buildBreakEvenQueries == std::numeric_limits<quint64>::max()) {
        output << "not_reached";
    } else {
        output << buildBreakEvenQueries;
    }
    output << ",refresh_break_even_queries=";
    if (refreshBreakEvenQueries == std::numeric_limits<quint64>::max()) {
        output << "not_reached";
    } else {
        output << refreshBreakEvenQueries;
    }
    output << '\n';
    return true;
}

void ConfigureProductionDisplayCacheView(CanvasView *view,
                                         CanvasDocument *document,
                                         const CanvasVector &worldQuery,
                                         bool bypassProductionCache)
{
    if (!view) {
        return;
    }

    view->resize(kViewSize);
    view->setUpdatesEnabled(false);
    view->SetGridVisible(false);
    view->SetAxisVisible(false);
    view->SetRulerVisible(false);
    view->SetSnapEnabled(false);
    view->SetDocument(document);
    view->CenterOnWorld(worldQuery);
    view->SetHitTestMaximumDisplaySizeCacheBypassedForProfiling(
                bypassProductionCache);
}

bool CaptureProductionDisplayCacheSample(
        QTextStream &output,
        CanvasView *view,
        CanvasDocument *document,
        const QPointF &viewportQuery,
        bool includeReleaseInTiming,
        bool cacheEnabled,
        int recursiveEntityCount,
        bool expectedHit,
        ItemId expectedItemId,
        const QString &caseName,
        const QString &modeName,
        int sampleIndex,
        QVector<qreal> *elapsedSamples,
        QVector<ItemId> *selection,
        ProductionDisplayCacheCapacitySample *capacitySample = nullptr)
{
    if (!view || !document || !elapsedSamples || !selection) {
        return false;
    }

    document->ClearSelection();
    view->ResetHitTestProfile();
    if (cacheEnabled && capacitySample) {
        ReadCurrentProcessPrivateUsageBytes(
                    &capacitySample->privateUsageBeforeBytes);
    }
    QElapsedTimer timer;
    timer.start();
    SendMouseEvent(view, QEvent::MouseButtonPress, viewportQuery,
                   Qt::LeftButton, Qt::LeftButton);
    if (includeReleaseInTiming) {
        SendMouseEvent(view, QEvent::MouseButtonRelease, viewportQuery,
                       Qt::LeftButton, Qt::NoButton);
    }
    const qreal elapsedMilliseconds = timer.nsecsElapsed() / 1000000.0;
    const CanvasView::CanvasHitTestProfile profile =
            view->GetHitTestProfile();

    if (!includeReleaseInTiming) {
        SendMouseEvent(view, QEvent::MouseButtonRelease, viewportQuery,
                       Qt::LeftButton, Qt::NoButton);
    }
    *selection = document->GetSelectedItemIds();
    if (cacheEnabled && capacitySample) {
        capacitySample->cacheEntryCount =
                profile.maximumDisplaySizeCacheEntryCount;
        capacitySample->cacheHashCapacity =
                profile.maximumDisplaySizeCacheCapacity;
        ReadCurrentProcessPrivateUsageBytes(
                    &capacitySample->privateUsageAfterBytes);
    }

    const quint64 hitTestCount = includeReleaseInTiming ? 2 : 1;
    const quint64 expectedEntityVisits =
            static_cast<quint64>(document->GetItemCount()) * hitTestCount;
    const quint64 expectedBoundsChecks =
            static_cast<quint64>(recursiveEntityCount) * hitTestCount;
    const quint64 expectedCacheLookups = cacheEnabled
            ? expectedBoundsChecks : 0;
    const quint64 expectedLiveCalculations = cacheEnabled
            ? 0 : expectedBoundsChecks;
    const bool profileValid = profile.hitTestCallCount == hitTestCount
            && profile.itemIdVisitCount == expectedEntityVisits
            && profile.boundsCheckCount == expectedBoundsChecks
            && profile.geometryBoundsCacheLookupCount == 0
            && profile.geometryBoundsCacheMissCount == 0
            && profile.maximumDisplaySizeCacheLookupCount
                    == expectedCacheLookups
            && profile.maximumDisplaySizeCacheMissCount == 0
            && profile.maximumDisplaySizeCalculationCallCount
                    == expectedLiveCalculations;
    const bool selectionValid = expectedHit == !selection->isEmpty()
            && (expectedItemId == 0
                || (selection->size() == 1
                    && selection->first() == expectedItemId));
    if (!profileValid || !selectionValid) {
        output << "ERROR,production_display_cache_invariant_failed,"
               << caseName << ',' << modeName << ',' << sampleIndex
               << ",hit_tests=" << profile.hitTestCallCount
               << ",bounds_checks=" << profile.boundsCheckCount
               << ",cache_lookups="
               << profile.maximumDisplaySizeCacheLookupCount
               << ",cache_misses="
               << profile.maximumDisplaySizeCacheMissCount
               << ",live_calculations="
               << profile.maximumDisplaySizeCalculationCallCount
               << ",selected=" << selection->size() << '\n';
        return false;
    }

    elapsedSamples->append(elapsedMilliseconds);
    return true;
}

bool CaptureProductionDisplayCachePair(
        QTextStream &output,
        CanvasView *cacheView,
        CanvasView *liveView,
        CanvasDocument *document,
        const QPointF &viewportQuery,
        bool includeReleaseInTiming,
        int recursiveEntityCount,
        bool expectedHit,
        ItemId expectedItemId,
        const QString &caseName,
        const QString &phaseName,
        int sampleIndex,
        QVector<qreal> *cacheSamples,
        QVector<qreal> *liveSamples,
        ProductionDisplayCacheCapacitySample *capacitySample = nullptr)
{
    QVector<ItemId> cacheSelection;
    QVector<ItemId> liveSelection;
    const bool cacheFirst = sampleIndex % 2 == 0;
    bool cacheValid = false;
    bool liveValid = false;
    if (cacheFirst) {
        cacheValid = CaptureProductionDisplayCacheSample(
                    output, cacheView, document, viewportQuery,
                    includeReleaseInTiming, true, recursiveEntityCount,
                    expectedHit, expectedItemId, caseName,
                    phaseName + QStringLiteral("_cache_on"), sampleIndex,
                    cacheSamples, &cacheSelection, capacitySample);
        liveValid = CaptureProductionDisplayCacheSample(
                    output, liveView, document, viewportQuery,
                    includeReleaseInTiming, false, recursiveEntityCount,
                    expectedHit, expectedItemId, caseName,
                    phaseName + QStringLiteral("_live_control"), sampleIndex,
                    liveSamples, &liveSelection);
    } else {
        liveValid = CaptureProductionDisplayCacheSample(
                    output, liveView, document, viewportQuery,
                    includeReleaseInTiming, false, recursiveEntityCount,
                    expectedHit, expectedItemId, caseName,
                    phaseName + QStringLiteral("_live_control"), sampleIndex,
                    liveSamples, &liveSelection);
        cacheValid = CaptureProductionDisplayCacheSample(
                    output, cacheView, document, viewportQuery,
                    includeReleaseInTiming, true, recursiveEntityCount,
                    expectedHit, expectedItemId, caseName,
                    phaseName + QStringLiteral("_cache_on"), sampleIndex,
                    cacheSamples, &cacheSelection, capacitySample);
    }

    if (!cacheValid || !liveValid || cacheSelection != liveSelection) {
        output << "ERROR,production_display_cache_selection_parity_failed,"
               << caseName << ',' << phaseName << ',' << sampleIndex << '\n';
        return false;
    }
    return true;
}

void PrintProductionDisplayCacheProfile(QTextStream &output,
                                        const QString &caseName,
                                        int recursiveEntityCount)
{
    output << "# MG011_PRODUCTION_CACHE_PROFILE,case=" << caseName
           << ",cache_on_lookups_per_hit_test=" << recursiveEntityCount
           << ",cache_on_live_scans_per_hit_test=0"
           << ",cache_on_misses=0"
           << ",live_control_cache_hits_per_hit_test=0"
           << ",live_control_scans_per_hit_test="
           << recursiveEntityCount << '\n';
}

bool MeasureProductionDisplayCacheCase(
        QTextStream &output,
        CanvasDocument *document,
        int entityCount,
        int curvePercent,
        const QString &caseName,
        const CanvasVector &worldQuery,
        int recursiveEntityCount,
        bool expectedHit,
        ItemId expectedItemId,
        bool measureInvalidationRefresh,
        ProductionDisplayCacheCapacitySample *capacitySample = nullptr,
        ProductionDisplayCacheTimingSummary *timingSummary = nullptr)
{
    if (!document || document->GetItemCount() != entityCount
            || recursiveEntityCount < 0
            || recursiveEntityCount > entityCount) {
        return false;
    }

    const QPointF viewportQuery = [&]() {
        CanvasView mappingView;
        ConfigureProductionDisplayCacheView(
                    &mappingView, document, worldQuery, false);
        return mappingView.MapFromWorld(worldQuery);
    }();
    QVector<qreal> coldCacheSamples;
    QVector<qreal> coldLiveSamples;
    coldCacheSamples.reserve(kMeasuredSamples);
    coldLiveSamples.reserve(kMeasuredSamples);

    for (int sample = 0; sample < kMeasuredSamples; ++sample) {
        CanvasView cacheView;
        CanvasView liveView;
        ConfigureProductionDisplayCacheView(
                    &cacheView, document, worldQuery, false);
        ConfigureProductionDisplayCacheView(
                    &liveView, document, worldQuery, true);
        if (!CaptureProductionDisplayCachePair(
                    output, &cacheView, &liveView, document, viewportQuery,
                    false, recursiveEntityCount, expectedHit, expectedItemId,
                    caseName, QStringLiteral("cold_first_press"), sample,
                    &coldCacheSamples, &coldLiveSamples,
                    sample == 0 ? capacitySample : nullptr)) {
            return false;
        }
    }

    CanvasView cacheView;
    CanvasView liveView;
    ConfigureProductionDisplayCacheView(
                &cacheView, document, worldQuery, false);
    ConfigureProductionDisplayCacheView(
                &liveView, document, worldQuery, true);

    QVector<qreal> warmCacheSamples;
    QVector<qreal> warmLiveSamples;
    for (int sample = 0; sample < kWarmupSamples; ++sample) {
        if (!CaptureProductionDisplayCachePair(
                    output, &cacheView, &liveView, document, viewportQuery,
                    true, recursiveEntityCount, expectedHit, expectedItemId,
                    caseName, QStringLiteral("warmup_click"), sample,
                    &warmCacheSamples, &warmLiveSamples)) {
            return false;
        }
    }
    warmCacheSamples.clear();
    warmLiveSamples.clear();

    for (int sample = 0; sample < kMeasuredSamples; ++sample) {
        if (!CaptureProductionDisplayCachePair(
                    output, &cacheView, &liveView, document, viewportQuery,
                    true, recursiveEntityCount, expectedHit, expectedItemId,
                    caseName, QStringLiteral("warm_click"), sample,
                    &warmCacheSamples, &warmLiveSamples)) {
            return false;
        }
    }

    const BenchmarkResult coldCacheResult = MakeBoundsCacheResult(
                QStringLiteral("MG011_ProductionDisplayCache"), entityCount,
                curvePercent, caseName + QStringLiteral("_cache_on_cold_first_press"),
                1, coldCacheSamples);
    const BenchmarkResult coldLiveResult = MakeBoundsCacheResult(
                QStringLiteral("MG011_ProductionDisplayCache"), entityCount,
                curvePercent, caseName + QStringLiteral("_live_control_cold_first_press"),
                1, coldLiveSamples);
    const BenchmarkResult warmCacheResult = MakeBoundsCacheResult(
                QStringLiteral("MG011_ProductionDisplayCache"), entityCount,
                curvePercent, caseName + QStringLiteral("_cache_on_warm_click"),
                1, warmCacheSamples);
    const BenchmarkResult warmLiveResult = MakeBoundsCacheResult(
                QStringLiteral("MG011_ProductionDisplayCache"), entityCount,
                curvePercent, caseName + QStringLiteral("_live_control_warm_click"),
                1, warmLiveSamples);
    PrintResult(output, coldCacheResult);
    PrintResult(output, coldLiveResult);
    PrintResult(output, warmCacheResult);
    PrintResult(output, warmLiveResult);

    qreal refreshCacheMedian = 0.0;
    qreal refreshCacheP95 = 0.0;
    qreal refreshLiveMedian = 0.0;
    qreal refreshLiveP95 = 0.0;
    if (measureInvalidationRefresh) {
        const QVector<ItemId> ids = document->GetItemIds();
        if (ids.isEmpty()) {
            return false;
        }
        const ItemId mutationId = ids.first();
        QVector<qreal> refreshCacheSamples;
        QVector<qreal> refreshLiveSamples;
        refreshCacheSamples.reserve(kMeasuredSamples);
        refreshLiveSamples.reserve(kMeasuredSamples);
        for (int sample = 0; sample < kMeasuredSamples; ++sample) {
            const CanvasEntity *current = document->GetEntity(mutationId);
            QSharedPointer<CanvasEntity> replacement = current
                    ? current->Clone()
                    : QSharedPointer<CanvasEntity>();
            const qreal displaySize = sample % 2 == 0 ? 24.0 : 4.0;
            if (!replacement
                    || !SetBenchmarkHitDisplaySize(replacement.data(),
                                                   displaySize)
                    || !document->ReplaceEntity(mutationId, *replacement)) {
                output << "ERROR,production_display_cache_refresh_mutation_failed,"
                       << caseName << ',' << sample << '\n';
                return false;
            }

            if (!CaptureProductionDisplayCachePair(
                        output, &cacheView, &liveView, document,
                        viewportQuery, false, recursiveEntityCount,
                        expectedHit, expectedItemId, caseName,
                        QStringLiteral("post_replace_first_press"), sample,
                        &refreshCacheSamples, &refreshLiveSamples)) {
                return false;
            }
        }

        const BenchmarkResult refreshCacheResult = MakeBoundsCacheResult(
                    QStringLiteral("MG011_ProductionDisplayCache"),
                    entityCount, curvePercent,
                    caseName + QStringLiteral("_cache_on_post_replace_first_press"),
                    1, refreshCacheSamples);
        const BenchmarkResult refreshLiveResult = MakeBoundsCacheResult(
                    QStringLiteral("MG011_ProductionDisplayCache"),
                    entityCount, curvePercent,
                    caseName + QStringLiteral("_live_control_post_replace_first_press"),
                    1, refreshLiveSamples);
        PrintResult(output, refreshCacheResult);
        PrintResult(output, refreshLiveResult);
        refreshCacheMedian = refreshCacheResult.medianMilliseconds;
        refreshCacheP95 = refreshCacheResult.p95Milliseconds;
        refreshLiveMedian = refreshLiveResult.medianMilliseconds;
        refreshLiveP95 = refreshLiveResult.p95Milliseconds;
    }

    const qreal coldBuildOverhead = coldCacheResult.medianMilliseconds
            - coldLiveResult.medianMilliseconds;
    const qreal warmQuerySavings = warmLiveResult.medianMilliseconds
            - warmCacheResult.medianMilliseconds;
    const qreal refreshOverhead = refreshCacheMedian - refreshLiveMedian;
    output << "# MG011_PRODUCTION_CACHE_ANALYSIS,case=" << caseName
           << ",cold_cache_overhead_ms="
           << QString::number(coldBuildOverhead, 'f', 6)
           << ",warm_query_savings_ms="
           << QString::number(warmQuerySavings, 'f', 6)
           << ",cold_break_even_queries=";
    const quint64 coldBreakEvenQueries = BoundsCacheBreakEvenQueries(
                coldBuildOverhead, warmQuerySavings);
    if (coldBreakEvenQueries == std::numeric_limits<quint64>::max()) {
        output << "not_reached";
    } else {
        output << coldBreakEvenQueries;
    }
    if (measureInvalidationRefresh) {
        output << ",post_replace_cache_overhead_ms="
               << QString::number(refreshOverhead, 'f', 6)
               << ",post_replace_break_even_queries=";
        const quint64 refreshBreakEvenQueries = BoundsCacheBreakEvenQueries(
                    refreshOverhead, warmQuerySavings);
        if (refreshBreakEvenQueries == std::numeric_limits<quint64>::max()) {
            output << "not_reached";
        } else {
            output << refreshBreakEvenQueries;
        }
    }
    output << '\n';
    PrintProductionDisplayCacheProfile(output, caseName,
                                       recursiveEntityCount);
    if (timingSummary) {
        timingSummary->coldCacheMedianMilliseconds =
                coldCacheResult.medianMilliseconds;
        timingSummary->coldCacheP95Milliseconds =
                coldCacheResult.p95Milliseconds;
        timingSummary->coldLiveMedianMilliseconds =
                coldLiveResult.medianMilliseconds;
        timingSummary->coldLiveP95Milliseconds =
                coldLiveResult.p95Milliseconds;
        timingSummary->warmCacheMedianMilliseconds =
                warmCacheResult.medianMilliseconds;
        timingSummary->warmCacheP95Milliseconds =
                warmCacheResult.p95Milliseconds;
        timingSummary->warmLiveMedianMilliseconds =
                warmLiveResult.medianMilliseconds;
        timingSummary->warmLiveP95Milliseconds =
                warmLiveResult.p95Milliseconds;
        timingSummary->refreshCacheMedianMilliseconds = refreshCacheMedian;
        timingSummary->refreshCacheP95Milliseconds = refreshCacheP95;
        timingSummary->refreshLiveMedianMilliseconds = refreshLiveMedian;
        timingSummary->refreshLiveP95Milliseconds = refreshLiveP95;
    }
    return true;
}

bool PopulateMixedCacheCapacityDocument(
        CanvasDocument *document,
        int totalEntityCount,
        int recursiveEntityPercent,
        int polylineSegmentCount,
        int groupChildCount,
        int *recursiveEntityCount,
        ItemId *probeItemId,
        QString *failureReason)
{
    if (!document || !recursiveEntityCount || !probeItemId
            || totalEntityCount <= 0 || recursiveEntityPercent <= 0
            || recursiveEntityPercent > 100
            || (totalEntityCount * recursiveEntityPercent) % 100 != 0
            || polylineSegmentCount < 3 || groupChildCount < 3) {
        if (failureReason) {
            *failureReason = QStringLiteral("invalid_parameters");
        }
        return false;
    }

    const int recursiveCount =
            totalEntityCount * recursiveEntityPercent / 100;
    if (recursiveCount < 2 || recursiveCount % 2 != 0) {
        if (failureReason) {
            *failureReason = QStringLiteral("recursive_count_must_be_even");
        }
        return false;
    }

    int simpleEntityIndex = 0;
    ItemId firstRecursiveEntityId = 0;
    for (int index = 0; index < totalEntityCount; ++index) {
        if (index < recursiveCount) {
            const bool addPolyline = index % 2 == 0;
            const CanvasEntityType type = addPolyline
                    ? CanvasEntityType::Polyline
                    : CanvasEntityType::GroupContainer;
            const CanvasVector center = index == 0
                    ? kInteractionProbeCenter
                    : CanvasVector(300000.0 + index * 64.0,
                                   -300000.0 - index * 64.0);
            const int complexity = addPolyline
                    ? polylineSegmentCount : groupChildCount;
            if (!AddBoundsComplexityEntity(document, type, center,
                                           index == 0, complexity)) {
                if (failureReason) {
                    *failureReason = QStringLiteral("recursive_entity_add_failed_%1")
                            .arg(index);
                }
                return false;
            }
            if (index == 0) {
                const QVector<ItemId> ids = document->GetItemIds();
                if (ids.isEmpty()) {
                    if (failureReason) {
                        *failureReason = QStringLiteral("probe_entity_missing");
                    }
                    return false;
                }
                firstRecursiveEntityId = ids.last();
            }
        } else {
            if (!AddBenchmarkEntity(document, simpleEntityIndex++,
                                    SceneMix::Simple)) {
                if (failureReason) {
                    *failureReason = QStringLiteral("simple_entity_add_failed_%1")
                            .arg(simpleEntityIndex - 1);
                }
                return false;
            }
        }
    }

    int actualRecursiveCount = 0;
    int actualPolylineCount = 0;
    int actualGroupCount = 0;
    for (const ItemId id : document->GetItemIds()) {
        const CanvasEntity *entity = document->GetEntity(id);
        if (!entity) {
            if (failureReason) {
                *failureReason = QStringLiteral("missing_entity_%1")
                        .arg(static_cast<qulonglong>(id));
            }
            return false;
        }
        if (entity->GetType() == CanvasEntityType::Polyline) {
            ++actualRecursiveCount;
            ++actualPolylineCount;
        } else if (entity->GetType() == CanvasEntityType::GroupContainer) {
            ++actualRecursiveCount;
            ++actualGroupCount;
        }
    }

    if (document->GetItemCount() != totalEntityCount
            || actualRecursiveCount != recursiveCount
            || actualPolylineCount != recursiveCount / 2
            || actualGroupCount != recursiveCount / 2
            || firstRecursiveEntityId == 0) {
        if (failureReason) {
            *failureReason = QStringLiteral("mixed_scene_counts_invalid");
        }
        return false;
    }

    *recursiveEntityCount = recursiveCount;
    *probeItemId = firstRecursiveEntityId;
    return true;
}

bool RunMG012CacheCapacitySample(int argc, char *argv[], QTextStream &output)
{
    if (argc != 7) {
        output << "ERROR,mg012_capacity_sample_argument_count," << argc << '\n';
        return false;
    }

    const QString caseName = QString::fromLocal8Bit(argv[2]);
    bool totalOk = false;
    bool percentOk = false;
    bool polylineComplexityOk = false;
    bool groupComplexityOk = false;
    const int totalEntityCount = QString::fromLocal8Bit(argv[3])
            .toInt(&totalOk);
    const int recursiveEntityPercent = QString::fromLocal8Bit(argv[4])
            .toInt(&percentOk);
    const int polylineSegmentCount = QString::fromLocal8Bit(argv[5])
            .toInt(&polylineComplexityOk);
    const int groupChildCount = QString::fromLocal8Bit(argv[6])
            .toInt(&groupComplexityOk);
    if (!totalOk || !percentOk || !polylineComplexityOk
            || !groupComplexityOk || caseName.isEmpty()) {
        output << "ERROR,mg012_capacity_sample_invalid_arguments\n";
        return false;
    }

    CanvasDocument document;
    int recursiveEntityCount = 0;
    ItemId probeItemId = 0;
    QString failureReason;
    if (!PopulateMixedCacheCapacityDocument(
                &document, totalEntityCount, recursiveEntityPercent,
                polylineSegmentCount, groupChildCount,
                &recursiveEntityCount, &probeItemId, &failureReason)) {
        output << "ERROR,mg012_capacity_scene_build_failed," << caseName
               << ',' << failureReason << '\n';
        return false;
    }

    QByteArray detailOutput;
    QTextStream detailStream(&detailOutput, QIODevice::WriteOnly);
    ProductionDisplayCacheCapacitySample capacitySample;
    ProductionDisplayCacheTimingSummary timingSummary;
    if (!MeasureProductionDisplayCacheCase(
                detailStream, &document, totalEntityCount,
                recursiveEntityPercent, caseName, kInteractionProbeCenter,
                recursiveEntityCount, true, probeItemId, true,
                &capacitySample, &timingSummary)) {
        detailStream.flush();
        output << "ERROR,mg012_capacity_measurement_failed," << caseName
               << '\n' << detailOutput;
        return false;
    }
    detailStream.flush();

    if (capacitySample.cacheEntryCount
                != static_cast<quint64>(recursiveEntityCount)
            || capacitySample.cacheHashCapacity
                < capacitySample.cacheEntryCount) {
        output << "ERROR,mg012_capacity_cache_invariant_failed," << caseName
               << ",expected_entries=" << recursiveEntityCount
               << ",actual_entries=" << capacitySample.cacheEntryCount
               << ",hash_capacity=" << capacitySample.cacheHashCapacity
               << '\n';
        return false;
    }

    const quint64 payloadBytesLowerBound =
            capacitySample.cacheEntryCount
            * static_cast<quint64>(sizeof(ItemId) + sizeof(qreal));
    output << "MG012_CACHE_CAPACITY_SAMPLE,case=" << caseName
           << ",total_entities=" << totalEntityCount
           << ",recursive_percent=" << recursiveEntityPercent
           << ",recursive_entities=" << recursiveEntityCount
           << ",polyline_segments=" << polylineSegmentCount
           << ",group_children=" << groupChildCount
           << ",cache_entries=" << capacitySample.cacheEntryCount
           << ",qhash_capacity=" << capacitySample.cacheHashCapacity
           << ",key_value_payload_lower_bound_bytes="
           << payloadBytesLowerBound
           << ",private_usage_before_bytes=";
    if (capacitySample.privateUsageBeforeBytes >= 0) {
        output << capacitySample.privateUsageBeforeBytes;
    } else {
        output << "unavailable";
    }
    output << ",private_usage_after_bytes=";
    if (capacitySample.privateUsageAfterBytes >= 0) {
        output << capacitySample.privateUsageAfterBytes;
    } else {
        output << "unavailable";
    }
    output << ",private_usage_delta_bytes=";
    if (capacitySample.privateUsageBeforeBytes >= 0
            && capacitySample.privateUsageAfterBytes >= 0) {
        output << capacitySample.privateUsageAfterBytes
                  - capacitySample.privateUsageBeforeBytes;
    } else {
        output << "unavailable";
    }
    output << ",cold_cache_median_ms="
           << QString::number(timingSummary.coldCacheMedianMilliseconds,
                               'f', 6)
           << ",cold_live_median_ms="
           << QString::number(timingSummary.coldLiveMedianMilliseconds,
                               'f', 6)
           << ",warm_cache_median_ms="
           << QString::number(timingSummary.warmCacheMedianMilliseconds,
                               'f', 6)
           << ",warm_cache_p95_ms="
           << QString::number(timingSummary.warmCacheP95Milliseconds,
                               'f', 6)
           << ",warm_live_median_ms="
           << QString::number(timingSummary.warmLiveMedianMilliseconds,
                               'f', 6)
           << ",warm_live_p95_ms="
           << QString::number(timingSummary.warmLiveP95Milliseconds,
                               'f', 6)
           << ",post_replace_cache_median_ms="
           << QString::number(timingSummary.refreshCacheMedianMilliseconds,
                               'f', 6)
           << ",post_replace_live_median_ms="
           << QString::number(timingSummary.refreshLiveMedianMilliseconds,
                               'f', 6)
           << ",selection_parity=true,operation_counts_verified=true\n";
    output.flush();
    return true;
}

bool RunMG012CacheCapacityExperiment(QTextStream &output)
{
    struct ExperimentCase
    {
        const char *name;
        int totalEntityCount;
        int recursiveEntityPercent;
        int polylineSegmentCount;
        int groupChildCount;
    };
    const ExperimentCase cases[] = {
        {"mixed_1k_1pct_base", 1000, 1, 64, 16},
        {"mixed_1k_10pct_base", 1000, 10, 64, 16},
        {"mixed_1k_50pct_base", 1000, 50, 64, 16},
        {"mixed_5k_1pct_base", 5000, 1, 64, 16},
        {"mixed_5k_10pct_base", 5000, 10, 64, 16},
        {"mixed_5k_50pct_base", 5000, 50, 64, 16},
        {"mixed_10k_1pct_base", 10000, 1, 64, 16},
        {"mixed_10k_10pct_base", 10000, 10, 64, 16},
        {"mixed_10k_50pct_base", 10000, 50, 64, 16},
        {"mixed_5k_10pct_heavy", 5000, 10, 256, 64},
        {"mixed_5k_50pct_heavy", 5000, 50, 256, 64}
    };
    const int caseCount = sizeof(cases) / sizeof(cases[0]);
    int completedCaseCount = 0;
    output << "# MG012_CACHE_CAPACITY_EXPERIMENT"
              ",scope=performancebench_only"
              ",scene=simple_entities_mixed_with_polyline_and_group"
              ",recursive_ratios=1|10|50_percent"
              ",baseline_complexity=polyline_64_segments_group_16_children"
              ",heavy_complexity=polyline_256_segments_group_64_children"
              ",memory=isolated_child_process_private_usage_delta"
              ",samples=31,selection_and_operation_count_parity_required=true\n";

    for (int caseIndex = 0; caseIndex < caseCount; ++caseIndex) {
        const ExperimentCase &experimentCase = cases[caseIndex];
        output << "# MG012_CASE_BEGIN,case=" << experimentCase.name << '\n';
        output.flush();

        QStringList arguments;
        arguments << QStringLiteral("--mg012-cache-capacity-sample")
                  << QString::fromLatin1(experimentCase.name)
                  << QString::number(experimentCase.totalEntityCount)
                  << QString::number(experimentCase.recursiveEntityPercent)
                  << QString::number(experimentCase.polylineSegmentCount)
                  << QString::number(experimentCase.groupChildCount);
        QProcess process;
        process.setProcessChannelMode(QProcess::SeparateChannels);
        process.start(QCoreApplication::applicationFilePath(), arguments);
        if (!process.waitForStarted(15000)
                || !process.waitForFinished(300000)) {
            process.kill();
            process.waitForFinished();
            output << "ERROR,mg012_child_process_timeout," << experimentCase.name
                   << ',' << process.errorString() << '\n';
            return false;
        }

        const QByteArray childOutput = process.readAllStandardOutput();
        const QByteArray childError = process.readAllStandardError();
        if (process.exitStatus() != QProcess::NormalExit
                || process.exitCode() != 0) {
            output << "ERROR,mg012_child_process_failed," << experimentCase.name
                   << ",exit_code=" << process.exitCode() << '\n'
                   << childError;
            return false;
        }

        QByteArray sampleLine;
        const QList<QByteArray> lines = childOutput.split('\n');
        for (const QByteArray &line : lines) {
            const QByteArray trimmedLine = line.trimmed();
            if (trimmedLine.startsWith("MG012_CACHE_CAPACITY_SAMPLE,")) {
                sampleLine = trimmedLine;
                break;
            }
        }
        if (sampleLine.isEmpty()) {
            output << "ERROR,mg012_child_sample_output_missing,"
                   << experimentCase.name << '\n' << childOutput << childError;
            return false;
        }

        output << sampleLine << '\n';
        output.flush();
        ++completedCaseCount;
    }

    output << "# MG012_CACHE_CAPACITY_SUMMARY,cases=" << completedCaseCount
           << ",expected_cases=" << caseCount
           << ",all_scenes_mixed=true"
           << ",cache_entries_match_recursive_root_count=true"
           << ",isolated_memory_measurements=true"
           << ",selection_parity=true,operation_counts_verified=true\n";
    output.flush();
    return completedCaseCount == caseCount;
}

bool RunMG011ProductionDisplayCacheExperiment(QTextStream &output)
{
    struct ExperimentCase
    {
        const char *name;
        CanvasEntityType entityType;
        int entityCount;
        int complexity;
        SceneMix mix;
        bool dense;
        bool hit;
        bool refresh;
        ItemId expectedItemId;
    };
    const ExperimentCase cases[] = {
        {"polyline_256_hit", CanvasEntityType::Polyline, 1000, 256,
         SceneMix::Simple, false, true, false, 1},
        {"polyline_256_miss", CanvasEntityType::Polyline, 1000, 256,
         SceneMix::Simple, false, false, true, 0},
        {"group_64_hit", CanvasEntityType::GroupContainer, 1000, 64,
         SceneMix::Simple, false, true, false, 1},
        {"group_64_miss", CanvasEntityType::GroupContainer, 1000, 64,
         SceneMix::Simple, false, false, true, 0},
        {"simple_sparse_hit", CanvasEntityType::Invalid, 5000, 0,
         SceneMix::Simple, false, true, false, 2}
    };
    const int caseCount = sizeof(cases) / sizeof(cases[0]);
    int completedCaseCount = 0;
    output << "# MG011_PRODUCTION_DISPLAY_CACHE_EXPERIMENT"
              ",scope=performancebench_only"
              ",cache_on=production_default"
              ",live_control=production_cache_bypassed_for_profiling"
              ",both_modes_keep_default_selective_bounds_filter=true"
              ",cold=first_mouse_press_on_fresh_view"
              ",warm=mouse_click_after_cache_warmup"
              ",refresh=first_mouse_press_after_document_replace"
              ",samples=31"
              ",paired_order=alternating_same_process"
              ",selection_and_operation_count_parity_required=true\n";

    for (int caseIndex = 0; caseIndex < caseCount; ++caseIndex) {
        const ExperimentCase &experimentCase = cases[caseIndex];
        const QString caseName = QString::fromLatin1(experimentCase.name);
        CanvasDocument document;
        bool sceneValid = false;
        if (experimentCase.entityType == CanvasEntityType::Invalid) {
            sceneValid = PopulateDocument(
                        &document, experimentCase.entityCount,
                        experimentCase.mix, experimentCase.dense);
            if (sceneValid && experimentCase.hit && !experimentCase.dense) {
                sceneValid = PrepareInteractionProbe(&document);
            }
        } else {
            QString failureReason;
            sceneValid = PopulateBoundsComplexityDocument(
                        &document, experimentCase.entityType,
                        experimentCase.entityCount, 1,
                        experimentCase.complexity, &failureReason);
            if (!sceneValid) {
                output << "ERROR,production_display_cache_scene_build_failed,"
                       << caseName << ',' << failureReason << '\n';
                return false;
            }
        }
        if (!sceneValid) {
            output << "ERROR,production_display_cache_scene_build_failed,"
                   << caseName << '\n';
            return false;
        }

        const int curvePercent = experimentCase.entityType
                == CanvasEntityType::Invalid
                ? CurvePercentage(experimentCase.mix)
                : BoundsEntityCurvePercentage(experimentCase.entityType);
        const CanvasVector worldQuery = experimentCase.hit
                ? kInteractionProbeCenter
                : CanvasVector(1000000.0, 1000000.0);
        const int recursiveEntityCount =
                experimentCase.entityType == CanvasEntityType::Polyline
                || experimentCase.entityType
                        == CanvasEntityType::GroupContainer
                ? experimentCase.entityCount : 0;
        if (!MeasureProductionDisplayCacheCase(
                    output, &document, experimentCase.entityCount,
                    curvePercent, caseName, worldQuery, recursiveEntityCount,
                    experimentCase.hit, experimentCase.expectedItemId,
                    experimentCase.refresh)) {
            output << "ERROR,production_display_cache_case_failed,"
                   << caseName << '\n';
            return false;
        }
        ++completedCaseCount;
        output.flush();
    }

    output << "# MG011_PRODUCTION_DISPLAY_CACHE_SUMMARY,cases="
           << completedCaseCount << ",expected_cases=" << caseCount
           << ",selection_parity=true"
           << ",cold_warm_refresh_stages_measured=true"
           << ",production_default_path_exercised=true"
           << ",live_control_preserves_default_bounds_filter=true\n";
    output.flush();
    return completedCaseCount == caseCount;
}

bool RunMG010MaximumDisplaySizeExperiment(QTextStream &output)
{
    if (!VerifyMaximumDisplaySizeCacheMutationParity(output)) {
        return false;
    }

    struct ExperimentCase
    {
        const char *name;
        int entityCount;
        SceneMix mix;
        CanvasEntityType entityType;
        int complexity;
        bool dense;
        bool hit;
    };
    const ExperimentCase cases[] = {
        {"simple_sparse_hit", 5000, SceneMix::Simple,
         CanvasEntityType::Invalid, 0, false, true},
        {"simple_dense_hit", 5000, SceneMix::Simple,
         CanvasEntityType::Invalid, 0, true, true},
        {"curve_heavy_miss", 5000, SceneMix::CurveHeavy,
         CanvasEntityType::Invalid, 0, false, false},
        {"polyline_256_miss", 1000, SceneMix::Simple,
         CanvasEntityType::Polyline, 256, false, false},
        {"group_64_miss", 1000, SceneMix::Simple,
         CanvasEntityType::GroupContainer, 64, false, false}
    };
    const int caseCount = sizeof(cases) / sizeof(cases[0]);
    int completedCaseCount = 0;
    output << "# MG010_MAXIMUM_DISPLAY_SIZE_EXPERIMENT"
              ",scope=profiling_build_only"
              ",modes=filter_off|live_geometry_live_style|cached_geometry_live_style|cached_geometry_cached_style"
              ",hierarchy_cases=1000_entities"
              ",mixed_cases=5000_entities"
              ",samples_per_mode=31"
              ",stage_timing_sample=6_of_36_weighted_by_6"
              ",operation_counts=exact"
              ",mutation_refresh=replace_one_entity_then_rebuild_full_cache"
              ",production_cache_enabled=false"
              ",selection_parity_required=true\n";

    for (int caseIndex = 0; caseIndex < caseCount; ++caseIndex) {
        const ExperimentCase &experimentCase = cases[caseIndex];
        const QString caseName = QString::fromLatin1(experimentCase.name);
        CanvasDocument document;
        bool sceneValid = false;
        if (experimentCase.entityType == CanvasEntityType::Invalid) {
            sceneValid = PopulateDocument(
                        &document, experimentCase.entityCount,
                        experimentCase.mix, experimentCase.dense);
            if (sceneValid && experimentCase.hit && !experimentCase.dense) {
                sceneValid = PrepareInteractionProbe(&document);
            }
        } else {
            QString sceneFailureReason;
            sceneValid = PopulateBoundsComplexityDocument(
                        &document, experimentCase.entityType,
                        experimentCase.entityCount, 1,
                        experimentCase.complexity, &sceneFailureReason);
            if (!sceneValid) {
                output << "ERROR,display_size_cache_scene_build_failed,"
                       << caseName << ',' << sceneFailureReason << '\n';
                return false;
            }
        }
        if (!sceneValid) {
            output << "ERROR,display_size_cache_scene_build_failed," << caseName
                   << '\n';
            return false;
        }

        const int curvePercent = experimentCase.entityType
                == CanvasEntityType::Invalid
                ? CurvePercentage(experimentCase.mix)
                : BoundsEntityCurvePercentage(experimentCase.entityType);
        const CanvasVector query = experimentCase.hit
                ? kInteractionProbeCenter
                : CanvasVector(1000000.0, 1000000.0);
        BenchmarkResult modeResults[4];
        BenchmarkResult cacheBuildResult;
        BenchmarkResult replaceOnlyResult;
        BenchmarkResult replaceAndRebuildResult;
        if (!MeasureHitDisplaySizeCacheCase(
                    output, &document, experimentCase.entityCount,
                    static_cast<int>(curvePercent), caseName, query,
                    experimentCase.hit, modeResults, &cacheBuildResult,
                    &replaceOnlyResult, &replaceAndRebuildResult)) {
            output << "ERROR,display_size_cache_case_failed," << caseName << '\n';
            return false;
        }
        ++completedCaseCount;
        output.flush();
    }

    output << "# MG010_MAXIMUM_DISPLAY_SIZE_SUMMARY,cases="
           << completedCaseCount
           << ",expected_cases=" << caseCount
           << ",timing_rows=" << completedCaseCount * 7
           << ",four_way_selection_parity=true"
           << ",style_tree_visit_counts_verified=true"
           << ",cache_misses=0"
           << ",wide_and_narrow_style_mutation_parity=true"
           << ",post_replace_full_cache_rebuild_measured=true"
           << ",production_cache_enabled=false\n";
    output.flush();
    return completedCaseCount == caseCount;
}

bool RunMG010HitFilterCacheExperiment(QTextStream &output)
{
    if (!VerifyBoundsFilterHitParity(output)) {
        return false;
    }

    int completedCaseCount = 0;
    const int mixedEntityCount = 5000;
    const SceneMix mixes[] = {SceneMix::Simple, SceneMix::CurveHeavy};
    output << "# MG010_HIT_FILTER_CACHE_MATRIX"
              ",entity_count=5000"
              ",mixed_workloads=sparse_hit|miss|dense_overlap_hit"
              ",modes=filter_off|filter_live_bounds|filter_cached_geometry_bounds"
              ",cache_scope=raw_geometry_bounds_only"
              ",maximum_display_size=recomputed"
              ",hit_tolerance=unchanged"
              ",samples_per_mode=31"
              ",paired_order=rotating_same_process\n";
    for (SceneMix mix : mixes) {
        CanvasDocument sparseDocument;
        if (!PopulateDocument(&sparseDocument, mixedEntityCount, mix)
                || !PrepareInteractionProbe(&sparseDocument)
                || !MeasureBoundsFilterAba(
                    output, &sparseDocument, mixedEntityCount, mix,
                    BoundsFilterWorkload::SparseHit, -1,
                    CanvasEntityType::Invalid, 0, QString(), -1, true)
                || !MeasureBoundsFilterAba(
                    output, &sparseDocument, mixedEntityCount, mix,
                    BoundsFilterWorkload::Miss, -1,
                    CanvasEntityType::Invalid, 0, QString(), -1, true)) {
            output << "ERROR,hit_filter_cache_mixed_sparse_failed,"
                   << CurvePercentage(mix) << '\n';
            return false;
        }
        completedCaseCount += 2;

        CanvasDocument denseDocument;
        if (!PopulateDocument(&denseDocument, mixedEntityCount, mix, true)
                || !MeasureBoundsFilterAba(
                    output, &denseDocument, mixedEntityCount, mix,
                    BoundsFilterWorkload::DenseHit, -1,
                    CanvasEntityType::Invalid, 0, QString(), -1, true)) {
            output << "ERROR,hit_filter_cache_mixed_dense_failed,"
                   << CurvePercentage(mix) << '\n';
            return false;
        }
        ++completedCaseCount;
    }

    const CanvasEntityType entityTypes[] = {
        CanvasEntityType::Point,
        CanvasEntityType::Line,
        CanvasEntityType::Arc,
        CanvasEntityType::Circle,
        CanvasEntityType::Ellipse,
        CanvasEntityType::Polyline,
        CanvasEntityType::Spline,
        CanvasEntityType::GroupContainer
    };
    const int entityTypeCount = sizeof(entityTypes) / sizeof(entityTypes[0]);
    const int entityTypeEntityCount = 1000;
    const int entityTypeCandidatePercentages[] = {1, 100};
    const int entityTypeCandidateCount = sizeof(entityTypeCandidatePercentages)
            / sizeof(entityTypeCandidatePercentages[0]);
    output << "# MG010_HIT_FILTER_CACHE_ENTITY_TYPES"
              ",entity_count=1000"
              ",entity_types=Point|Line|Arc|Circle|Ellipse|Polyline|Spline|GroupContainer"
              ",candidate_percentages=1|100"
              ",exact_hit_identity_verified=true\n";
    for (CanvasEntityType type : entityTypes) {
        for (int candidateIndex = 0;
             candidateIndex < entityTypeCandidateCount;
             ++candidateIndex) {
            const int candidatePercent =
                    entityTypeCandidatePercentages[candidateIndex];
            CanvasDocument document;
            QString sceneFailureReason;
            if (!PopulateBoundsEntityTypeDocument(
                        &document, type, entityTypeEntityCount,
                        candidatePercent, &sceneFailureReason)) {
                output << "ERROR,hit_filter_cache_entity_scene_failed,"
                       << CanvasEntityTypeName(type) << ','
                       << candidatePercent << ',' << sceneFailureReason << '\n';
                return false;
            }

            const QVector<ItemId> ids = document.GetItemIds();
            const ItemId expectedHitId = ids.isEmpty() ? 0 : ids.first();
            if (expectedHitId == 0
                    || !MeasureBoundsFilterAba(
                        output, &document, entityTypeEntityCount,
                        SceneMix::Simple,
                        BoundsFilterWorkload::SelectivityHit,
                        candidatePercent, type, expectedHitId,
                        QString(), -1, true)) {
                output << "ERROR,hit_filter_cache_entity_measurement_failed,"
                       << CanvasEntityTypeName(type) << ','
                       << candidatePercent << '\n';
                return false;
            }
            ++completedCaseCount;
        }
    }

    struct ComplexityCase
    {
        CanvasEntityType entityType;
        int complexity;
        const char *dimension;
        int selectionPathsPerEntity;
    };
    const ComplexityCase complexityCases[] = {
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
    const int complexityCaseCount = sizeof(complexityCases)
            / sizeof(complexityCases[0]);
    const int complexityEntityCount = 5000;
    const int complexityCandidatePercent = 1;
    output << "# MG010_HIT_FILTER_CACHE_COMPLEXITY"
              ",entity_count=5000"
              ",candidate_percent=1"
              ",polyline_segments=3|16|64|256"
              ",spline_control_points=3|16|64"
              ",group_children=3|16|64"
              ",same_complexity_for_candidate_and_non_candidate_entities=true\n";
    for (int caseIndex = 0; caseIndex < complexityCaseCount; ++caseIndex) {
        const ComplexityCase &complexityCase = complexityCases[caseIndex];
        const QString complexityLabel = QStringLiteral("%1_%2")
                .arg(QString::fromLatin1(complexityCase.dimension))
                .arg(complexityCase.complexity);
        CanvasDocument document;
        QString sceneFailureReason;
        if (!PopulateBoundsComplexityDocument(
                    &document, complexityCase.entityType,
                    complexityEntityCount, complexityCandidatePercent,
                    complexityCase.complexity, &sceneFailureReason)) {
            output << "ERROR,hit_filter_cache_complexity_scene_failed,"
                   << CanvasEntityTypeName(complexityCase.entityType) << ','
                   << complexityLabel << ',' << sceneFailureReason << '\n';
            return false;
        }

        const QVector<ItemId> ids = document.GetItemIds();
        const ItemId expectedHitId = ids.isEmpty() ? 0 : ids.first();
        if (expectedHitId == 0
                || !MeasureBoundsFilterAba(
                    output, &document, complexityEntityCount,
                    SceneMix::Simple,
                    BoundsFilterWorkload::SelectivityHit,
                    complexityCandidatePercent, complexityCase.entityType,
                    expectedHitId, complexityLabel,
                    complexityCase.selectionPathsPerEntity, true)) {
            output << "ERROR,hit_filter_cache_complexity_measurement_failed,"
                   << CanvasEntityTypeName(complexityCase.entityType) << ','
                   << complexityLabel << '\n';
            return false;
        }
        ++completedCaseCount;
        output.flush();
    }

    const int expectedCaseCount = 6 + entityTypeCount * entityTypeCandidateCount
            + complexityCaseCount;
    output << "# MG010_HIT_FILTER_CACHE_SUMMARY,cases="
           << completedCaseCount
           << ",expected_cases=" << expectedCaseCount
           << ",timing_rows=" << completedCaseCount * 3
           << ",three_way_selection_parity=true"
           << ",cached_bounds_candidate_parity=true"
           << ",cache_misses=0"
           << ",maximum_display_size_recomputed=true"
           << ",hit_tolerance_unchanged=true"
           << ",production_cache_enabled=false\n";
    output.flush();
    return completedCaseCount == expectedCaseCount;
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
                    == QStringLiteral("--mg012-cache-capacity-sample")) {
        if (!RunMG012CacheCapacitySample(argc, argv, output)) {
            output.flush();
            return 2;
        }
        return 0;
    }

    if (argc > 1
            && QString::fromLocal8Bit(argv[1])
                    == QStringLiteral("--mg012-cache-capacity")) {
        if (!RunMG012CacheCapacityExperiment(output)) {
            output.flush();
            return 2;
        }
        return 0;
    }

    if (argc > 1
            && QString::fromLocal8Bit(argv[1])
                    == QStringLiteral("--mg011-production-display-cache")) {
        if (!RunMG011ProductionDisplayCacheExperiment(output)) {
            output.flush();
            return 2;
        }
        return 0;
    }

    if (argc > 1
            && QString::fromLocal8Bit(argv[1])
                    == QStringLiteral("--mg010-hit-display-cache")) {
        output << "# MG010_HIT_DISPLAY_CACHE_EXPERIMENT"
                  ",scope=profiling_build_only"
                  ",production_cache_enabled=false"
                  ",bounds_filter_default=off"
                  ",style_scan_and_cache_build_refresh=true"
                  ",paired_order=rotating_same_process\n";
        if (!RunMG010MaximumDisplaySizeExperiment(output)) {
            output.flush();
            return 2;
        }
        return 0;
    }

    if (argc > 1
            && QString::fromLocal8Bit(argv[1])
                    == QStringLiteral("--mg010-hit-filter-cache")) {
        output << "# MG010_HIT_FILTER_CACHE_EXPERIMENT"
                  ",scope=profiling_build_only"
                  ",runtime_bounds_filter_default=off"
                  ",production_cache_enabled=false"
                  ",entity_type_and_complexity_matrix=true"
                  ",paired_order=rotating_same_process\n";
        if (!RunMG010HitFilterCacheExperiment(output)) {
            output.flush();
            return 2;
        }
        return 0;
    }

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
