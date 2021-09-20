
// #include "ANPV.hpp"
#include "DecoderFactory.hpp"
#include "Image.hpp"
#include "UserCancellation.hpp"

#include <QApplication>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QFutureWatcher>
#include <QPixmap>
#include <QGraphicsPixmapItem>
#include <QSplashScreen>
#include <QScreen>
#include <QtDebug>
#include <QFileInfo>
#include <QMainWindow>
#include <QStatusBar>
#include <QProgressBar>
#include <QPromise>
#include <QDir>

#include <chrono>
#include <thread>

using namespace std::chrono_literals;

/* compile with:
 * g++ -g -Wall example.cc `pkg-config vips-cpp --cflags --libs`
 */


#if 0
#define QT_NO_KEYWORDS
#define QT_NO_SIGNALS_SLOTS_KEYWORDS

#include <vips/vips8>

#undef QT_NO_SIGNALS_SLOTS_KEYWORDS
#undef QT_NO_KEYWORDS

#include <QApplication>
#include <QLabel>
#include <QImage>
using namespace vips;


static void
conversion_preeval( VipsImage *image, VipsProgress *progress, void *data )
{
        qInfo() << "Preeval " << progress->percent << " ETA: " << progress->eta;
}

static void
conversion_eval( VipsImage *image, VipsProgress *progress, void *data )
{
        qInfo() << "Eval " << progress->percent << " ETA: " << progress->eta;
}

static void
conversion_posteval( VipsImage *image, VipsProgress *progress, void *data )
{
    qInfo() << "PostEval " << progress->percent << " ETA: " << progress->eta;
}


int
main (int argc, char **argv)
{
    QApplication a(argc, argv);
    
  if (VIPS_INIT (argv[0])) 
    vips_error_exit (NULL);

  if (argc != 2)
    vips_error_exit ("usage: %s input-file", argv[0]);

  VImage in = VImage::new_from_file (argv[1],
    VImage::option ()->set ("access", VIPS_ACCESS_SEQUENTIAL));

//   in = VImage::thumbnail(argv[1],
//                  2000
// //                  ,VImage::option()->set("crop", VIPS_INTERESTING_ATTENTION)
//                     ,VImage::option()->set("no_rotate", true)
//                         );
  
  in = in.colourspace(VIPS_INTERPRETATION_sRGB, NULL);
  
//   in = in.extract_area(0,0,2000,642);
  
  
  auto form = in.format();
  auto bands = in.bands();
//   auto pages = in.get_n_pages(); // vips_image_get_n_pages()
  
  size_t rowStride = VIPS_IMAGE_SIZEOF_LINE(in.get_image());
//   size_t lineStride = VIPS_REGION_LSKIP();

  
    vips_image_set_progress(in.get_image(), true);
    
    guint preeval_sig = g_signal_connect( in.get_image(),
                                          "preeval",
                                          G_CALLBACK( conversion_preeval ),
                                          nullptr );
    guint eval_sig = g_signal_connect( in.get_image(), 
                "eval",
                G_CALLBACK( conversion_eval ), 
                nullptr );
    guint posteval_sig = g_signal_connect( in.get_image(), 
                "posteval",
                G_CALLBACK( conversion_posteval ), 
                nullptr );
  
    VImage rgbOut = VImage::new_memory();
    std::unique_ptr<VipsRegion, decltype(&g_object_unref)> rgbRegion(vips_region_new(rgbOut.get_image()), ::g_object_unref);
    
    vips_sink_screen( in.get_image(), rgbOut.get_image(), nullptr, 128,128, 400, 0,
                      [](VipsImage *out, VipsRect *rect, void *r)
                      {
                          VipsRegion* reg = static_cast<VipsRegion*>(r);
                          vips_region_prepare(reg, rect);
                          VipsPel* pix = VIPS_REGION_ADDR_TOPLEFT(reg);
                          int i=0;
                      }
                      , rgbRegion.get() );
  
  size_t len;
  std::unique_ptr<unsigned char, decltype(&g_free)> rgb(static_cast<unsigned char*>(in.write_to_memory(&len)), ::g_free);
    QImage myImage(rgb.get(), in.width(), in.height(), rowStride, bands == 4 ? QImage::Format_RGBA8888 : QImage::Format_RGB888);

    QLabel myLabel;
    myLabel.setPixmap(QPixmap::fromImage(myImage));

    myLabel.show();

    int ret =  a.exec();
    
    
    if( preeval_sig )
    { 
        g_signal_handler_disconnect( rgbOut.get_image(), preeval_sig );
        preeval_sig = 0;
    }

    if( eval_sig )
    { 
        g_signal_handler_disconnect( rgbOut.get_image(), eval_sig );
        eval_sig = 0;
    }

    if( posteval_sig )
    { 
        g_signal_handler_disconnect( rgbOut.get_image(), posteval_sig );
        posteval_sig = 0;
    }
    
  vips_shutdown ();
  return ret;
}

#else

#if 0
class SortedImageModel : public QRunnable
{
    std::vector<std::unique_ptr<Image>> entries;
public:
    std::unique_ptr<QPromise<DecodingState>> directoryWorker = std::make_unique<QPromise<DecodingState>>();
    
    QDir currentDir;
    
    void throwIfDirectoryLoadingCancelled()
    {
        if(directoryWorker->isCanceled())
        {
            throw UserCancellation();
        }
    }
    
    bool addSingleFile(const QFileInfo& inf)
    {
        entries.emplace_back(std::make_unique<Image>(inf));
        return true;
    }
    
    void run() override
    {
        int entriesProcessed = 0;
        try
        {
            this->directoryWorker->start();
//             this->setStatusMessage(0, "Clearing old entries");
            this->entries.clear();
            
//             this->setStatusMessage(0, "Looking up directory");
            QFileInfoList fileInfoList = this->currentDir.entryInfoList(QDir::AllEntries | QDir::NoDotAndDotDot);
            const int entriesToProcess = fileInfoList.size();
            if(entriesToProcess > 0)
            {
                this->directoryWorker->setProgressRange(0, entriesToProcess + 1 /* for sorting effort */);
                
//                 QString msg = QString("Loading %1 directory entries").arg(entriesToProcess);
//                 if (this->sortedColumnNeedsPreloadingMetadata())
//                 {
//                     msg += " and reading EXIF data (making it quite slow)";
//                 }
                
//                 this->setStatusMessage(0, msg);
                this->entries.reserve(entriesToProcess);
                this->entries.shrink_to_fit();
                unsigned readableImages = 0;
                while (!fileInfoList.isEmpty())
                {
                    do
                    {
                        QFileInfo inf = fileInfoList.takeFirst();
                        if(this->addSingleFile(inf))
                        {
                            ++readableImages;
                        }
                    } while (false);

                    this->throwIfDirectoryLoadingCancelled();
//                     this->setStatusMessage(entriesProcessed++, msg);
                }

//                 this->setStatusMessage(entriesProcessed++, "Almost done: Sorting entries, please wait...");
//                 this->sortEntries();
//                 if(this->sortOrder == Qt::DescendingOrder)
//                 {
//                     this->reverseEntries();
//                 }
                
//                 this->setStatusMessage(entriesProcessed, QString("Directory successfully loaded; discovered %1 readable images of a total of %2 entries").arg(readableImages).arg(entriesToProcess));
//                 QMetaObject::invokeMethod(this, [&](){ this->onDirectoryLoaded(); }, Qt::QueuedConnection);
            }
            else
            {
                this->directoryWorker->setProgressRange(0, 1);
//                 this->setStatusMessage(1, "Directory is empty, nothing to see here.");
            }
                
            this->directoryWorker->addResult(DecodingState::FullImage);
//             this->watcher->addPath(this->currentDir.absolutePath());
        }
        catch (const UserCancellation&)
        {
            this->directoryWorker->addResult(DecodingState::Cancelled);
        }
        catch (const std::exception& e)
        {
//             this->setStatusMessage(entriesProcessed, QString("Exception occurred while loading the directory: %1").arg(e.what()));
            this->directoryWorker->addResult(DecodingState::Error);
        }
        catch (...)
        {
//             this->setStatusMessage(entriesProcessed, "Fatal error occurred while loading the directory");
            this->directoryWorker->addResult(DecodingState::Error);
        }
//         this->endRemoveRows();
        this->directoryWorker->finish();
    }
};



#include <QStandardItemModel>
#include <QVector>
#include <QTreeView>
#include <QListView>

void appendPath(QAbstractItemModel* const model, QStringList& fileParts, const QModelIndex& parent = QModelIndex()){
	Q_ASSERT(model);
	if(fileParts.isEmpty())
		return;
	if(model->columnCount(parent)<=0)
		model->insertColumn(0,parent);
	QModelIndex currIdx;
	const QString currLevel = fileParts.takeFirst();
	const int rowCount = model->rowCount(parent);
	for(int i=0;i<rowCount;++i){
		if(model->index(i,0,parent).data().toString().compare(currLevel)==0){
			currIdx = model->index(i,0,parent);
			break;
		}
	}
	if(!currIdx.isValid()){
		model->insertRow(rowCount,parent);
		currIdx=model->index(rowCount,0,parent);
		model->setData(currIdx,currLevel);
	}
	appendPath(model,fileParts,currIdx);
}


int main(int argc, char *argv[])
{
    Q_INIT_RESOURCE(ANPV);
    QApplication a(argc, argv);
    
    QSplashScreen splash(QPixmap(":/images/splash.jpg"));
    splash.show();
    
    // create and init DecoderFactory in main thread
    (void)DecoderFactory::globalInstance();
    
    
    
    QStandardItemModel *model = new QStandardItemModel(); //give a parent or you'll leak

    QVector<QString> entries({
        {"text/english/game/pro_scen.msg"},
        {"text/english/game/pro_wall.msg"},
        {"text/english/game/proto.msg"},
        {"text/english/game/quests.msg"},
        {"example/english/game/pro_scen.msg"},
        {"script.int"}
    });
    for(const QString& entry : entries)
    {
        QStringList fileParts = entry.split("/");
        appendPath(model,fileParts);
    }
    
    QTreeView* treeView = new QTreeView();
    treeView->setModel(model);
//     treeView->setRootIsDecorated(false);
//     treeView->expandAll();
//     treeView->show();
    
    QListView* listView = new QListView();
    listView->setViewMode(QListView::IconMode);
    listView->setSelectionBehavior(QAbstractItemView::SelectRows);
    listView->setSelectionMode(QAbstractItemView::ExtendedSelection);
    listView->setModel(model);
    listView->show();
    
    return a.exec();

    
    
    
    
    
    
    
    
    QElapsedTimer tim;
    
    SortedImageModel dir;
    dir.currentDir = QDir(argv[1]);
    dir.setAutoDelete(false);
    
    QFutureWatcher<DecodingState> wat;
    wat.setFuture(dir.directoryWorker->future());
    QObject::connect(&wat, &QFutureWatcher<DecodingState>::started, [&](){ tim.start(); });
    QObject::connect(&wat, &QFutureWatcher<DecodingState>::finished,  [&](){ qInfo() << tim.elapsed(); });
    
    QThreadPool::globalInstance()->start(&dir);
    
    int r = a.exec();
    return r;
    
    
    
    
    
    
    
    
    
    ANPV m(&splash);
    m.show();
    splash.finish(&m);
    
    if(argc == 2)
    {
        QString arg(argv[1]);
        QFileInfo info(arg);
        if(info.exists())
        {
            if(info.isDir())
            {
                m.showThumbnailView();
                m.setThumbnailDir(arg);
            }
            else if(info.isFile())
            {
                m.showImageView();
                m.loadImage(info);
                splash.showMessage("Starting the image decoding task...");
            }
        }
        else
        {
            qCritical() << "Path '" << argv[1] << "' not found";
            return -1;
        }
    }
    else
    {
        qInfo() << "No directory given, assuming " << QDir::currentPath();
        m.showThumbnailView();
        m.setThumbnailDir(QDir::currentPath());
    }
    
    return a.exec();
}
#endif

#include "ANPV.hpp"

int main(int argc, char *argv[])
{
    Q_INIT_RESOURCE(ANPV);
    QApplication app(argc, argv);
    
    QSplashScreen splash(QPixmap(":/images/splash.jpg"));
    splash.show();
    
    // create and init DecoderFactory in main thread
    (void)DecoderFactory::globalInstance();

    
    
    ANPV a(&splash);
    
    int r = app.exec();
    return r;
}



#endif
