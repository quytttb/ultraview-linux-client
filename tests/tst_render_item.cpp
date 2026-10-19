//
// Tests for the custom scene-graph render item (Phase 5 requirement).
//
// `RemoteScreenItem` must be a plain QQuickItem that draws through the scene graph
// with a QSGSimpleTextureNode: no QQuickPaintedItem, no framebuffer object.  These
// tests drive the public item API that the QML viewer and the session both use:
// patch submission, the mirror the user actually sees, the coordinate mapping and
// the renderer notification that keeps the worker's in-flight window bounded.
//
// The GPU upload itself is exercised by the application self-test
// (`ultraview --self-test`, registered as `app_self_test`), because it needs a
// live scene graph.
//
#include "TestSupport.h"

#include "render/RemoteScreenItem.h"

#include <QQuickItem>
#include <QQuickWindow>
#include <QSGSimpleTextureNode>
#include <QSGTexture>
#include <QtTest>

using namespace ultraview::decoder;
using namespace ultraview::render;

namespace {

FrameUpdatePtr makeUpdate(const QSize &canvasSize, const QRect &rect, QRgb color, quint64 sequence,
                          bool fullFrame)
{
    auto update = std::make_shared<FrameUpdate>();
    update->sequence = sequence;
    update->canvasSize = canvasSize;
    update->rect = rect;
    update->patch = QImage(rect.size(), QImage::Format_RGB32);
    update->patch.fill(color);
    update->fullFrame = fullFrame;
    update->operations = 1;
    return update;
}

} // namespace

class TestRenderItem : public QObject {
    Q_OBJECT

private slots:
    void itemIsPlainQuickItem();
    void acceptsFullFramePatch();
    void blitsPatchesIntoTheMirror();
    void ignoresPatchesForAnotherCanvas();
    void clearsOnRequest();
    void coordinateMapping();
    void contentRectFollowsTheItem();
    void reportsConsumptionToTheWorker();
    void buildsSceneGraphNode();
};

void TestRenderItem::itemIsPlainQuickItem()
{
    RemoteScreenItem item;
    // The renderer must be a plain QQuickItem: neither of the two shortcuts the
    // requirements forbid may be used.
    QVERIFY(item.inherits("QQuickItem"));
    QVERIFY(!item.inherits("QQuickPaintedItem"));
    QVERIFY(!item.inherits("QQuickFramebufferObject"));
    QVERIFY(!item.hasContent());
    QCOMPARE(item.mirrorHash(), QByteArray());
    QCOMPARE(item.pendingTexturePatches(), 0);
    QCOMPARE(item.sourceSize(), QSize());
    QVERIFY(!item.contentRect().isValid());
}

void TestRenderItem::acceptsFullFramePatch()
{
    RemoteScreenItem item;
    item.setSize(QSizeF(64, 48));
    item.submitUpdate(makeUpdate(QSize(64, 48), QRect(0, 0, 64, 48), qRgb(10, 20, 30), 1, true));

    QVERIFY(item.hasContent());
    QCOMPARE(item.sourceSize(), QSize(64, 48));
    // The first patch always re-uploads the whole texture, so nothing is queued.
    QCOMPARE(item.pendingTexturePatches(), 0);
    QCOMPARE(item.mirrorRgb888().size(), 64 * 48 * 3);
    // The mirror matches the decoder's own dump for the same pixels.
    FrameBuffer reference;
    reference.reset(QSize(64, 48));
    reference.canvas().fill(QColor(10, 20, 30));
    QCOMPARE(item.mirrorHash(), reference.contentHash());
    QCOMPARE(item.mirrorRgb888(), reference.toRgb888());
}

void TestRenderItem::blitsPatchesIntoTheMirror()
{
    RemoteScreenItem item;
    item.setSize(QSizeF(1, 1));
    item.submitUpdate(makeUpdate(QSize(8, 4), QRect(0, 0, 8, 4), qRgb(0, 0, 0), 1, true));

    // A partial patch changes only its own rectangle and is queued for upload.
    item.submitUpdate(makeUpdate(QSize(8, 4), QRect(2, 1, 3, 2), qRgb(255, 0, 0), 2, false));
    QCOMPARE(item.pendingTexturePatches(), 1);
    FrameBuffer reference;
    reference.reset(QSize(8, 4));
    reference.canvas().fill(QColor(0, 0, 0));
    for (int y = 1; y <= 2; ++y) {
        for (int x = 2; x <= 4; ++x)
            reference.canvas().setPixel(x, y, qRgb(255, 0, 0));
    }
    QCOMPARE(item.mirrorHash(), reference.contentHash());

    // Whole-canvas patches replace everything, like the decoder's reset.
    item.submitUpdate(makeUpdate(QSize(8, 4), QRect(0, 0, 8, 4), qRgb(1, 2, 3), 3, true));
    FrameBuffer resized;
    resized.reset(QSize(8, 4));
    resized.canvas().fill(QColor(1, 2, 3));
    QCOMPARE(item.mirrorHash(), resized.contentHash());

    // A canvas size change (partner resolution change) replaces the mirror and
    // drops the queued patches of the previous canvas.
    item.submitUpdate(makeUpdate(QSize(4, 2), QRect(0, 0, 4, 2), qRgb(9, 9, 9), 4, true));
    QCOMPARE(item.sourceSize(), QSize(4, 2));
    QCOMPARE(item.pendingTexturePatches(), 0);
    FrameBuffer smaller;
    smaller.reset(QSize(4, 2));
    smaller.canvas().fill(QColor(9, 9, 9));
    QCOMPARE(item.mirrorHash(), smaller.contentHash());
}

void TestRenderItem::ignoresPatchesForAnotherCanvas()
{
    RemoteScreenItem item;
    item.setSize(QSizeF(10, 10));
    item.submitUpdate(makeUpdate(QSize(10, 10), QRect(0, 0, 10, 10), qRgb(5, 5, 5), 1, true));
    const QByteArray hash = item.mirrorHash();

    // A patch that does not fit the current mirror is refused instead of being
    // blitted at the wrong offset.
    item.submitUpdate(makeUpdate(QSize(64, 64), QRect(0, 0, 64, 64), qRgb(200, 0, 0), 2, false));
    QCOMPARE(item.mirrorHash(), hash);
    item.submitUpdate(makeUpdate(QSize(10, 10), QRect(8, 8, 4, 4), qRgb(0, 200, 0), 3, false));
    QCOMPARE(item.mirrorHash(), hash);
    // An empty patch never reaches the mirror either.
    item.submitUpdate(makeUpdate(QSize(10, 10), QRect(0, 0, 0, 0), qRgb(0, 0, 200), 4, false));
    QCOMPARE(item.mirrorHash(), hash);
    // A null update is ignored silently (the worker may publish nothing).
    item.submitUpdate(FrameUpdatePtr());
    QCOMPARE(item.mirrorHash(), hash);
}

void TestRenderItem::clearsOnRequest()
{
    RemoteScreenItem item;
    item.setSize(QSizeF(4, 4));
    item.submitUpdate(makeUpdate(QSize(4, 4), QRect(0, 0, 4, 4), qRgb(7, 7, 7), 1, true));
    QVERIFY(item.hasContent());
    item.clear();
    QVERIFY(!item.hasContent());
    QCOMPARE(item.sourceSize(), QSize());
    QCOMPARE(item.mirrorHash(), QByteArray());
    // Submitting after a clear() starts a fresh mirror.
    item.submitUpdate(makeUpdate(QSize(4, 4), QRect(0, 0, 4, 4), qRgb(3, 3, 3), 2, true));
    QVERIFY(item.hasContent());
}

void TestRenderItem::coordinateMapping()
{
    RemoteScreenItem item;
    item.setSize(QSizeF(800, 600));
    item.submitUpdate(makeUpdate(QSize(1920, 1080), QRect(0, 0, 1920, 1080), qRgb(0, 0, 0), 1, true));

    // Letter-boxed (fit): the same mapping rules the ViewportMap tests pin.
    const QRectF content = item.contentRect();
    QCOMPARE(content, QRectF(0, 75, 800, 450));
    const QPointF centre = item.mapToSource(400, 300);
    QCOMPARE(centre.x(), 960.0);
    QCOMPARE(centre.y(), 540.0);
    // Outside the displayed desktop the mapping reports (-1, -1).
    QCOMPARE(item.mapToSource(400, 10), QPointF(-1, -1));
    QCOMPARE(item.mapToSource(-5, 300), QPointF(-1, -1));
    // The inverse mapping places a remote pixel back into the item.
    const QPointF mapped = item.mapFromSource(960, 540);
    QCOMPARE(mapped.x(), 400.0);
    QCOMPARE(mapped.y(), 300.0);

    item.setFillMode(RemoteScreenItem::FillMode::Stretch);
    QCOMPARE(item.contentRect(), QRectF(0, 0, 800, 600));
    QCOMPARE(item.mapToSource(400, 300), QPointF(960, 540));
    QCOMPARE(item.mapToSource(0, 0), QPointF(0, 0));
    QCOMPARE(item.fillMode(), RemoteScreenItem::FillMode::Stretch);

    item.setFillMode(RemoteScreenItem::FillMode::PreserveAspectCrop);
    const QRectF cropped = item.contentRect();
    QCOMPARE(cropped.height(), 600.0);
    QVERIFY(qAbs(cropped.width() - 600.0 * (1920.0 / 1080.0)) < 0.01);
    QVERIFY(qAbs(cropped.x() + (cropped.width() - 800.0) / 2.0) < 0.01);
    QCOMPARE(cropped.y(), 0.0);
}

void TestRenderItem::contentRectFollowsTheItem()
{
    RemoteScreenItem item;
    QSignalSpy contentSpy(&item, &RemoteScreenItem::contentRectChanged);
    item.submitUpdate(makeUpdate(QSize(100, 100), QRect(0, 0, 100, 100), qRgb(0, 0, 0), 1, true));
    QVERIFY(contentSpy.count() >= 1);

    // QQuickItem::setSize() calls geometryChange(), which must refresh the
    // letter-boxed content rectangle the UI binds to.
    const int before = contentSpy.count();
    item.setSize(QSizeF(400, 100));
    QCOMPARE(item.contentRect(), QRectF(150, 0, 100, 100));
    QVERIFY(contentSpy.count() > before);

    item.setSmooth(false);
    QVERIFY(!item.isSmooth());
    QCOMPARE(item.contentRect(), QRectF(150, 0, 100, 100));
}

void TestRenderItem::reportsConsumptionToTheWorker()
{
    RemoteScreenItem item;
    QSignalSpy consumedSpy(&item, &RemoteScreenItem::framesConsumed);
    item.setSize(QSizeF(10, 10));
    item.submitUpdate(makeUpdate(QSize(10, 10), QRect(0, 0, 10, 10), qRgb(0, 0, 0), 1, true));
    // Every accepted patch is acknowledged exactly once, so the worker's in-flight
    // window cannot grow without bound.
    QCOMPARE(consumedSpy.count(), 1);

    for (int index = 0; index < 40; ++index) {
        item.submitUpdate(makeUpdate(QSize(10, 10), QRect(1, 1, 2, 2), qRgb(index, 0, 0),
                                     static_cast<quint64>(index + 2), false));
    }
    QCOMPARE(consumedSpy.count(), 41);
    // 40 queued patches would be unbounded; the item falls back to one full upload.
    QCOMPARE(item.pendingTexturePatches(), 0);
    QVERIFY(item.hasContent());
}

void TestRenderItem::buildsSceneGraphNode()
{
    // The real scene-graph path: with a window the item's updatePaintNode() is
    // called by Qt.  Offscreen platforms may not provide a working RHI backend, in
    // which case rendering (and therefore consumption) is reported instead of
    // failing the unit test; the application self-test asserts the pixels where a
    // backend exists.
    RemoteScreenItem item;
    item.setSize(QSizeF(32, 32));
    item.submitUpdate(makeUpdate(QSize(32, 32), QRect(0, 0, 32, 32), qRgb(200, 100, 50), 1, true));
    QCOMPARE(item.pendingTexturePatches(), 0);

    QQuickWindow window;
    window.resize(32, 32);
    item.setParentItem(window.contentItem());
    item.setPosition(QPointF(0, 0));
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    QTest::qWait(100);

    if (item.pendingTexturePatches() != 0)
        QWARN("the offscreen platform did not consume the texture patch; GPU path untested here");

    const QImage grabbed = window.grabWindow();
    if (!grabbed.isNull() && grabbed.size().width() >= 16 && grabbed.size().height() >= 16) {
        const QColor pixel = grabbed.pixelColor(16, 16);
        if (item.pendingTexturePatches() == 0) {
            // The patched colour must be visible; allow for the platform's colour
            // conversion (the self-test applies the same tolerance).
            QVERIFY2(qAbs(pixel.red() - 200) <= 8,
                     qPrintable(QStringLiteral("red channel %1").arg(pixel.red())));
            QVERIFY(qAbs(pixel.green() - 100) <= 8);
            QVERIFY(qAbs(pixel.blue() - 50) <= 8);
        }
    } else {
        QWARN("no window grab available on this platform");
    }
    window.hide();
}

QTEST_MAIN(TestRenderItem)
#include "tst_render_item.moc"
