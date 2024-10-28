/**************************************************************************
 *                                                                        *
 * SPDX-FileCopyrightText: 2016 Felix Rohrbach <kde@fxrh.de>              *
 *                                                                        *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *                                                                        *
 **************************************************************************/

#include "thumbnailprovider.h"

#include "timelinewidget.h"
#include "quaternionroom.h"
#include "logging_categories.h"

#include <Quotient/user.h>
#include <Quotient/jobs/mediathumbnailjob.h>

#include <QtCore/QCoreApplication> // for qApp
#include <QtCore/QReadWriteLock>
#include <QtCore/QThread>

using Quotient::BaseJob;

inline int checkDimension(int d)
{
    // Emulate ushort overflow if the value is -1 - may cause issues when
    // screen resolution becomes 100K+ each dimension :-D
    return d >= 0 ? d : std::numeric_limits<ushort>::max();
}

inline QDebug operator<<(QDebug dbg, const auto&& size)
    requires std::is_same_v<std::decay_t<decltype(size)>, QSize>
{
    QDebugStateSaver _(dbg);
    return dbg.nospace() << size.width() << 'x' << size.height();
}

class AbstractThumbnailResponse : public QQuickImageResponse {
    Q_OBJECT
public:
    AbstractThumbnailResponse(const TimelineWidget* timeline, QString id, QSize size)
        : timeline(timeline)
        , mediaId(std::move(id))
        , requestedSize({ checkDimension(size.width()), checkDimension(size.height()) })
    {
        qCDebug(THUMBNAILS).noquote() << mediaId << '@' << requestedSize << "requested";
        if (mediaId.isEmpty() || requestedSize.isEmpty()) {
            qCDebug(THUMBNAILS) << "Returning an empty thumbnail";
            finish(QImage(requestedSize, QImage::Format_Invalid));
            return;
        }
        result = tr("Image request is pending");
        // Start a request on the main thread, concluding the initialisation
        moveToThread(qApp->thread());
        QMetaObject::invokeMethod(this, &AbstractThumbnailResponse::startRequest);
        // From this point, access to `result` must be guarded by `lock`
    }

protected:
    // The two below run in the main thread, not QML thread
    virtual void startRequest() = 0;
    virtual void doCancel() {}

    using result_type = Quotient::Expected<QImage, QString>;

    void finish(const result_type& r)
    {
        {
            QWriteLocker _(&lock);
            result = r;
        }
        emit finished();
    }

    const TimelineWidget* const timeline;
    const QString mediaId{};
    const QSize requestedSize{};

private:
    Quotient::Expected<QImage, QString> result{};
    mutable QReadWriteLock lock{}; // Guards ONLY the above

    // The following overrides run in QML thread

    QQuickTextureFactory* textureFactory() const override
    {
        QReadLocker _(&lock);
        return QQuickTextureFactory::textureFactoryForImage(result.value_or(QImage()));
    }

    QString errorString() const override
    {
        QReadLocker _(&lock);
        return result.has_value() ? QString() : result.error();
    }

    void cancel() override
    {
        // Flip from QML thread to the main thread
        QMetaObject::invokeMethod(this, &AbstractThumbnailResponse::doCancel);
    }
};

namespace {
const auto NoConnectionError =
    AbstractThumbnailResponse::tr("No connection to perform image request");
}

class ThumbnailResponse : public AbstractThumbnailResponse {
    Q_OBJECT
public:
    using AbstractThumbnailResponse::AbstractThumbnailResponse;
    ~ThumbnailResponse() override = default;

private slots:
    void startRequest() override
    {
        Q_ASSERT(QThread::currentThread() == qApp->thread());

        const auto* currentRoom = timeline->currentRoom();
        if (!currentRoom) {
            finish(NoConnectionError);
            return;
        }

        // Save the future so that we could cancel it
        futureResult =
            Quotient::JobHandle(currentRoom->connection()->getThumbnail(mediaId, requestedSize))
                .then(this,
                    [this](const QImage& thumbnail) {
                        qCDebug(THUMBNAILS).noquote()
                            << "Thumbnail for" << mediaId
                            << "ready, actual size:" << thumbnail.size();
                        return result_type { thumbnail };
                    },
                    [this](const Quotient::MediaThumbnailJob* job) {
                        qCWarning(THUMBNAILS).nospace()
                            << "No valid thumbnail for" << mediaId << ": " << job->errorString();
                        return result_type { job->errorString() };
                    });
        // NB: Make sure to connect to any possible outcome including cancellation so that
        // the QML thread is not left stuck forever.
        futureResult
            .onCanceled([this] {
                qCDebug(THUMBNAILS) << "Request cancelled for" << mediaId;
                return tr("Image request has been cancelled"); // Turn it to an error
            })
            .then([this] (const result_type& r) { finish(r); });
    }

    void doCancel() override
    {
        futureResult.cancel();
    }

private:
    QFuture<Quotient::Expected<QImage, QString>> futureResult;
};

#include "thumbnailprovider.moc" // Because we define a Q_OBJECT in the cpp file

template <class ResponseT>
class ImageProviderTemplate : public QQuickAsyncImageProvider {
public:
    explicit ImageProviderTemplate(TimelineWidget* parent) : timeline(parent) {}

private:
    QQuickImageResponse* requestImageResponse(const QString& id,
                                              const QSize& requestedSize) override
    {
        return new ResponseT(timeline, id, requestedSize);
    }

    const TimelineWidget* const timeline;
    Q_DISABLE_COPY(ImageProviderTemplate)
};

QQuickAsyncImageProvider* makeThumbnailProvider(TimelineWidget* parent)
{
    return new ImageProviderTemplate<ThumbnailResponse>(parent);
}
