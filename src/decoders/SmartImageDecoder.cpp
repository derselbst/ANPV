
#include "SmartImageDecoder.hpp"

#include "UserCancellation.hpp"
#include "Formatter.hpp"
#include "ExifWrapper.hpp"
#include "xThreadGuard.hpp"
#include "Image.hpp"
#include "ANPV.hpp"

#include <KDCRAW/KDcraw>
#include <QtDebug>
#include <QPromise>
#include <QMetaMethod>
#include <QThreadPool>
#include <QIcon>
#include <QFileIconProvider>
#include <chrono>
#include <atomic>
#include <mutex>

struct SmartImageDecoder::Impl
{
    SmartImageDecoder* q;
    
    // assertNotDecoding() doesn't gives us a guarantee. But at least for deletion we need this guarantee.
    // That's the job of this mutex.
    mutable std::recursive_mutex asyncApiMtx;
    
    QScopedPointer<QPromise<DecodingState>> promise;
    // the targetState requested by decodeAsync() (not the final state reached!)
    DecodingState targetState;
    // the resolution requested by decodeAsync() (not the final resolution reached!)
    QSize desiredResolution;
    // the ROI requested by decodeAsync() (not the final ROI reached!)
    QRect roiRect;
    QString decodingMessage;
    int decodingProgress=0;
    
    QSharedPointer<Image> image;
    
    // May or may not contain (a part of) the encoded input file
    // It does for embedded JPEG preview in CR2
    QByteArray encodedInputFile;
    
    // DO NOT STORE THE QFILE DIRECTLY!
    // QFile is a QObject! It has thread affinity! Creating it by the worker thread and destrorying it by the UI thread will lead to memory corruption!
    QScopedPointer<QFile> file;
    qint64 encodedInputBufferSize = 0;
    const unsigned char* encodedInputBufferPtr = nullptr;
    
    Impl(SmartImageDecoder* q, QSharedPointer<Image> image) : q(q), image(image)
    {}
    
    void open(const QFileInfo& info)
    {
        if(this->file != nullptr && this->file->isOpen())
        {
            throw std::logic_error("File is already open!");
        }
        
        this->file.reset(new QFile(info.absoluteFilePath()));
        if (!this->file->open(QIODevice::ReadOnly))
        {
            throw std::runtime_error(Formatter() << "Unable to open file '" << info.absoluteFilePath().toStdString() << "'");
        }
    }
    
    DecodingState decodingState()
    {
        return q->image()->decodingState();
    }

    void releaseFullImage()
    {
        q->image()->setDecodedImage(QImage());
    }
    
    void setErrorMessage(const QString& err)
    {
        q->image()->setErrorMessage(err);
    }

    QString latestMessage()
    {
        return this->decodingMessage;
    }
};

SmartImageDecoder::SmartImageDecoder(QSharedPointer<Image> image) : d(std::make_unique<Impl>(this, image))
{
    image->setDecodingState(DecodingState::Ready);
    this->setAutoDelete(false);
}

SmartImageDecoder::~SmartImageDecoder()
{
    this->assertNotDecoding();
    std::lock_guard g(d->asyncApiMtx);
    d->releaseFullImage();
}

QSharedPointer<Image> SmartImageDecoder::image()
{
    return d->image;
}

void SmartImageDecoder::setDecodingState(DecodingState state)
{
    this->image()->setDecodingState(state);
}

void SmartImageDecoder::cancelCallback()
{
    if(d->promise && d->promise->isCanceled())
    {
        throw UserCancellation();
    }
}

void SmartImageDecoder::open()
{
    try
    {
        d->open(this->image()->fileInfo());
    }
    catch(const std::exception& e)
    {
        d->setErrorMessage(e.what());
        this->setDecodingState(DecodingState::Fatal);
        throw;
    }
}

// initializes the decoder, by reading as much of the file as necessary to know about most important information
void SmartImageDecoder::init()
{
    try
    {
        if(!d->file || !d->file->isOpen())
        {
            throw std::logic_error("Decoder must be opened for init()");
        }

        xThreadGuard g(d->file.data());
        
        // mmap() the file-> Do NOT use MAP_PRIVATE! See https://stackoverflow.com/a/7222430
        qint64 mapSize = d->file->size();
        const unsigned char* fileMapped = d->file->map(0, mapSize, QFileDevice::NoOptions);
        
        d->encodedInputBufferSize = mapSize;
        d->encodedInputBufferPtr = fileMapped;
        if(!this->image()->isRaw())
        {
            if(fileMapped == nullptr)
            {
                throw std::runtime_error(Formatter() << "Could not mmap() file '" << d->file->fileName().toStdString() << "', error was: " << d->file->errorString().toStdString());
            }
        }
        else
        {
            QString filePath = this->image()->fileInfo().absoluteFilePath();

            // use KDcraw for getting the embedded preview
            bool ret = KDcrawIface::KDcraw::loadEmbeddedPreview(d->encodedInputFile, filePath);

            if (!ret)
            {
                // if the embedded preview loading failed, load half preview instead.
                // That's slower but it works even for images containing
                // small (160x120px) or none embedded preview.
                if (!KDcrawIface::KDcraw::loadHalfPreview(d->encodedInputFile, filePath))
                {
                    throw std::runtime_error(Formatter() << "KDcraw failed to open RAW file '" << d->file->fileName().toStdString() << "'");
                }
            }
            
            d->encodedInputBufferPtr = reinterpret_cast<const unsigned char*>(d->encodedInputFile.constData());
            d->encodedInputBufferSize = d->encodedInputFile.size();
        }
        
        this->cancelCallback();
        
        this->decodeHeader(d->encodedInputBufferPtr, d->encodedInputBufferSize);
        
        QSharedPointer<ExifWrapper> exifWrapper(new ExifWrapper());
        // intentionally use the original file to read EXIF data, as this may not be available in d->encodedInputBuffer
        exifWrapper->loadFromData(QByteArray::fromRawData(reinterpret_cast<const char*>(fileMapped), mapSize));
        this->image()->setExif(exifWrapper);
        
        QImage thumb = this->image()->thumbnail();
        if(thumb.isNull())
        {
            thumb = exifWrapper->thumbnail();
            if(!thumb.isNull())
            {
                this->convertColorSpace(thumb, true);
                this->image()->setThumbnail(thumb);
            }
        }
        
        // initialize cache
        (void)this->image()->cachedAutoFocusPoints();

        this->setDecodingState(DecodingState::Metadata);
    }
    catch(const std::exception& e)
    {
        d->setErrorMessage(e.what());
        this->setDecodingState(DecodingState::Fatal);
        throw;
    }
}

// Do not wait for finished()
void SmartImageDecoder::cancelOrTake(QFuture<DecodingState> taskFuture)
{
    if (d->promise.isNull())
    {
        qDebug() << "There isn't anything to cancel.";
    }

    bool isFinished = taskFuture.isFinished();
    bool taken = QThreadPool::globalInstance()->tryTake(this);
    if(!isFinished)
    {
        if(!taken)
        {
            taskFuture.cancel();
        }
        else
        {
            // current decoder was taken from the pool and will therefore never emit finished event, even though some clients are relying on this...
            d->promise->start();
            this->setDecodingState(DecodingState::Cancelled);
            d->promise->addResult(d->decodingState());
            d->promise->finish();
        }
    }
}

// FIXME This function may not be called concurrently by multiple threads
QFuture<DecodingState> SmartImageDecoder::decodeAsync(DecodingState targetState, Priority prio, QSize desiredResolution, QRect roiRect)
{
    if(!(targetState == DecodingState::Metadata || targetState == DecodingState::PreviewImage || targetState == DecodingState::FullImage))
    {
        throw std::invalid_argument(Formatter() << "DecodingState '" << targetState << "' cannot be requested");
    }

    if(d->promise)
    {
        QFuture<DecodingState> taskFuture = d->promise->future();
        DecodingState imageState = this->image()->decodingState();
        
        if(targetState != DecodingState::PreviewImage && targetState == d->targetState && targetState == imageState)
        {
            // return already decoded stuff
            qDebug() << "Skipping decoding of image " << this->image()->fileInfo().fileName() << " and returning what we already have.";
            return taskFuture;
        }
        
        this->cancelOrTake(taskFuture);
        taskFuture.waitForFinished();
        this->reset();
    }

    std::lock_guard g(d->asyncApiMtx);

    d->targetState = targetState;
    d->desiredResolution = desiredResolution;
    d->roiRect = roiRect;
    d->promise.reset(new QPromise<DecodingState>());
    d->promise->setProgressRange(0, 100);
    QFuture<DecodingState> fut = d->promise->future();

    // The threadpool will take over this instance after calling start. From there on this instance must be seen as deleted and no further calls must be made
    // to any of its members! E.g. calling d->promise->future() afterwards actually led to horribly hard-to-reproduce use-after-frees in the past.
    QThreadPool::globalInstance()->start(this, static_cast<int>(prio));
    return fut;
}

void SmartImageDecoder::run()
{
    std::lock_guard g(d->asyncApiMtx);
    d->promise->start();

    try
    {
        // Before opening a file potentially located on a slow network drive, check whether we have already been cancelled.
        this->cancelCallback();

        this->open();
        this->decode(d->targetState, d->desiredResolution, d->roiRect);
    }
    catch (const UserCancellation&)
    {
        this->setDecodingState(DecodingState::Cancelled);
    }
    catch(...)
    {
        Formatter f;
        f << "Uncaught exception during SmartImageDecoder::run()!";
        QString err = f.str().c_str();
        d->setErrorMessage(err);
        qCritical() << err;
        this->setDecodingState(DecodingState::Fatal);
    }

    // Immediately close ourself once done. This is important to avoid resource leaks, when the 
    // event loop of the UI thread gets too busy and it'll take long to react on the finished() events.
    this->close();

    // this will not store the result if the future has been canceled already!
    d->promise->addResult(d->decodingState());
    d->promise->finish();
}

void SmartImageDecoder::decode(DecodingState targetState, QSize desiredResolution, QRect roiRect)
{
    try
    {
        this->cancelCallback();
        do
        {
            this->init();
            
            if(targetState == DecodingState::PreviewImage || targetState == DecodingState::FullImage)
            {
                QImage decodedImg = this->decodingLoop(desiredResolution, roiRect);
                
                // if this assert fails, either an unintended QImage::copy() happened, or an intended QImage::copy() happend but a call to this->image()->setDecodedImage() is missing,
                // or multiple decoders are concurrently decoding the same image.
                Q_ASSERT(this->image()->decodedImage().constBits() == decodedImg.constBits());
                
                // if thumbnail is still null and we've decoded not just a part of the image
                if (this->image()->thumbnail().isNull() && (!roiRect.isValid() || roiRect.contains(this->image()->fullResolutionRect())))
                {
                    QSize thumbnailSize;
                    static const QSize thumbnailSizeMax(ANPV::MaxIconHeight, ANPV::MaxIconHeight);
                    if(thumbnailSizeMax.width() * thumbnailSizeMax.height() < desiredResolution.width() * desiredResolution.height())
                    {
                        thumbnailSize = thumbnailSizeMax;
                    }
                    else
                    {
                        thumbnailSize = desiredResolution;
                    }
                    this->image()->setThumbnail(decodedImg.scaled(thumbnailSize, Qt::KeepAspectRatio, Qt::SmoothTransformation));
                }
            }
        } while(false);
    }
    catch(const UserCancellation&)
    {
        this->setDecodingState(DecodingState::Cancelled);
    }
    catch(const std::exception& e)
    {
        d->setErrorMessage(e.what());
        this->setDecodingState(DecodingState::Error);
    }
}

void SmartImageDecoder::convertColorSpace(QImage& image, bool silent)
{
    if(image.depth() != 32)
    {
        throw std::logic_error("SmartImageDecoder::convertColorSpace(): case not implemented");
    }
    
    static const QColorSpace srgbSpace(QColorSpace::SRgb);
    QColorSpace csp = this->image()->colorSpace();
    if (csp.isValid() && csp != srgbSpace)
    {
        this->setDecodingMessage("Transforming colorspace...");
        QColorTransform colorTransform = csp.transformationToColorSpace(srgbSpace);

        auto* dataPtr = image.constBits();
        const size_t width = image.width();
        const size_t height = image.height();
        const size_t yStride = static_cast<size_t>(std::ceil((384 * 1024.0) / width));
        for (size_t y = 0; y < height; y+=yStride)
        {
            auto& destPixel = const_cast<uchar*>(dataPtr)[y * width * sizeof(QRgb) + 0];
            auto linesToConvertNow = std::min(height - y, yStride);

            // Unfortunately, QColorTransform only allows to map single RGB values, but not an entire scanline.
            // Rather than using the private QColorTransform::apply() method, create QImage instances which contain a small part of the entire image
            // and use applyColorTransform in small chunks.
            // This also allows cancelling the transformation.
            QImage tempImg(&destPixel, width, linesToConvertNow, image.format(), nullptr, nullptr);
            tempImg.applyColorTransform(colorTransform);

            this->cancelCallback();
            if (!silent)
            {
                this->updatePreviewImage(QRect(0, y, width, yStride));
            }
        }
    }
}

void SmartImageDecoder::close()
{
    d->encodedInputBufferSize = 0;
    d->encodedInputBufferPtr = nullptr;
    if(d->file)
    {
        xThreadGuard g(d->file.data());
        d->file->close();
        d->file.reset();
    }
}

void SmartImageDecoder::reset()
{
    this->assertNotDecoding();
    std::lock_guard g(d->asyncApiMtx);
    
    d->setErrorMessage(QString());
    if(d->decodingState() == DecodingState::Fatal)
    {
        this->setDecodingState(DecodingState::Ready);
        return;
    }
    this->releaseFullImage();
}

void SmartImageDecoder::releaseFullImage()
{
    std::lock_guard g(d->asyncApiMtx);
    d->releaseFullImage();
    this->setDecodingState(DecodingState::Metadata);
}

void SmartImageDecoder::setDecodingMessage(QString&& msg)
{
    if(d->promise && d->decodingMessage != msg)
    {
        d->decodingMessage = std::move(msg);
        d->promise->setProgressValueAndText(d->decodingProgress, d->decodingMessage);
    }
}

void SmartImageDecoder::setDecodingProgress(int prog)
{
    if(d->promise && d->decodingProgress != prog)
    {
        d->decodingProgress = prog;
        d->promise->setProgressValueAndText(prog , d->decodingMessage);
    }
}

void SmartImageDecoder::updatePreviewImage(const QRect& r)
{
    this->image()->updatePreviewImage(r);
}

// Actually, this is gives no guarantee; e.g. it could be that the worker thread has just finished and calls QPromise::finished(). In there,
// Qt sets the QFuture's state to finished and only then starts to run the continuation. If in the meantime another thread deletes
// this object, it also deletes this QPromise, leaving the promise with a nasty use-after-free.
// Observed in Qt 6.2.1:
// Stack trace worker thread:
//     #0  0x00007ffff3bc518b in raise () from /lib64/libc.so.6
//     #1  0x00007ffff3bc6585 in abort () from /lib64/libc.so.6
//     #2  0x00000000004eca07 in __sanitizer::Abort() () at ../projects/compiler-rt/lib/sanitizer_common/sanitizer_posix_libcdep.cpp:155
//     #3  0x00000000004eb374 in __sanitizer::Die() () at ../projects/compiler-rt/lib/sanitizer_common/sanitizer_termination.cpp:58
//     #4  0x00000000004f95f9 in ~ScopedReport () at ../projects/compiler-rt/lib/ubsan/ubsan_diag.cpp:392
//     #5  0x000000000050062f in HandleDynamicTypeCacheMiss () at ../projects/compiler-rt/lib/ubsan/ubsan_handlers_cxx.cpp:82
//     #6  0x00000000004ffffa in __ubsan_handle_dynamic_type_cache_miss () at ../projects/compiler-rt/lib/ubsan/ubsan_handlers_cxx.cpp:87
//     #7  0x0000000000571982 in QFutureInterface<DecodingState>::reportFinished (this=this@entry=0x6020089945b0) at /usr/include/qt6/QtCore/qfutureinterface.h:267
//     #8  0x0000000000571801 in QPromise<DecodingState>::finish (this=this@entry=0x6020089945b0) at /usr/include/qt6/QtCore/qpromise.h:95
//     #9  0x0000000000661a7a in SmartImageDecoder::run (this=0x6030023f1c70) at /ANPV/src/decoders/SmartImageDecoder.cpp:275
//     #10 0x00007ffff591ee3d in QThreadPoolThread::run (this=0x6040002bda50) at /usr/src/debug/qt6-base-6.2.1-lp153.19.1.x86_64/src/corelib/thread/qthreadpool.cpp:99
//     #11 0x00007ffff5917909 in QThreadPrivate::start (arg=0x6040002bda50) at /usr/src/debug/qt6-base-6.2.1-lp153.19.1.x86_64/src/corelib/thread/qthread_unix.cpp:338
//     #12 0x00007ffff458da1a in start_thread () from /lib64/libpthread.so.0
//     #13 0x00007ffff3c8bd0f in clone () from /lib64/libc.so.6
// Stack trace of the main thread was already in loadImage() / showImage(), i.e. it was past clearScene(), so decoder has just been deleted.
// Hence, the only purpose of this function is to indicate some obvious programming error, like making calls to the decoder although it's still running...
void SmartImageDecoder::assertNotDecoding()
{
    if(!d->promise)
    {
        return;
    }
    
    bool isRun = d->promise->future().isRunning();
    if(isRun)
    {
        throw std::logic_error("Operation not allowed, decoding is still ongoing.");
    }
}

QImage SmartImageDecoder::allocateImageBuffer(uint32_t width, uint32_t height, QImage::Format format)
{
    const size_t needed = size_t(width) * height;
    const size_t rowStride = width * sizeof(uint32_t);
    try
    {
        this->setDecodingMessage("Allocating image output buffer");

        std::unique_ptr<uint32_t, decltype(&free)> mem(static_cast<uint32_t*>(calloc(needed, sizeof(uint32_t))), &::free);
        QImage image(reinterpret_cast<uint8_t*>(mem.get()), width, height, rowStride, format, &free, mem.get());
        mem.release();

        // enter the PreviewImage state, even if the image is currently blank, so listeners can start listening for decoding updates
        this->setDecodingState(DecodingState::PreviewImage);
        return image;
    }
    catch (const std::bad_alloc&)
    {
        throw std::runtime_error(Formatter() << "Unable to allocate " << (needed * sizeof(uint32_t)) / 1024. / 1024. << " MiB for the decoded image with dimensions " << width << "x" << height << " px");
    }
}
