
#include "DocumentView.hpp"

#include <QApplication>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QPixmap>
#include <QGraphicsPixmapItem>
#include <QSplashScreen>
#include <QScreen>
#include <QtDebug>

#include <chrono>
#include <thread>

extern "C"
{
    #include <jerror.h>
    #include <jpeglib.h>
}

using namespace std::chrono_literals;

QGraphicsPixmapItem* prev=nullptr;
QGraphicsScene* s;
DocumentView* v;
QPixmap *p;

void testBegin()
{
    if(prev)
        s->removeItem(prev);
    delete prev;
    prev = nullptr;
}

void test()
{
    if(p == nullptr)
        return;
    
    // get the area of what the user sees
     QRect viewportRect = v->viewport()->rect();
     
     // and map that rect to scene coordinates
     QRectF viewportRectScene = v->mapToScene(viewportRect).boundingRect();
     
     // the user might have zoomed out too far, crop the rect, as we are not interseted in the surrounding void
     QRectF visPixRect = viewportRectScene.intersected(v->sceneRect());
     
     // the "inverted zoom factor"
     // 1.0 means the pixmap is shown at native size
     // >1.0 means the user zoomed out
     // <1.0 mean the user zommed in and sees the individual pixels
     auto newScale = std::max(visPixRect.width() / viewportRect.width(), visPixRect.height() / viewportRect.height());
     
     qWarning() << newScale << "\n";
     
     if (newScale > 1.0)
     {
         QPixmap imgToScale;
         
         if(viewportRectScene.contains(v->sceneRect()))
         {
             // the user sees the entire image
             imgToScale = *p;
         }
         else
         {
             // the user sees a part of the image
             // crop the image to the visible part, so we don't need to scale the entire one
             imgToScale = p->copy(visPixRect.toAlignedRect());
         }
        QPixmap scaled = imgToScale.scaled(viewportRect.size(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
        
        prev = new QGraphicsPixmapItem(std::move(scaled));
        prev->setPos(visPixRect.topLeft());
        prev->setScale( newScale );
        s->addItem(prev);
     }
     else
     {
         qDebug() << "Skipping smooth pixmap scaling: Too far zoomed in";
     }
}

void decodeJPGBuffered(const char* file)
{

}

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
    
    QSplashScreen splash(QPixmap("/home/tom/EigeneProgramme/ANPV/splash.jpg"));
    splash.show();
        
    splash.showMessage("Loading the Image...");
//     QPixmap pix("/home/tom/Bilder/Testbilder/PIA23623.tif"); p = &pix;
    
    
    splash.showMessage("Loading Thumbnail");
//     QPixmap thumb("/home/tom/Bilder/Jena/panoJenzig/AAAD2760.dpp - AAAD2902.dpp_fused_enblend-preview1.jpg");
//     thumb = thumb.scaled(pix.width(), pix.height(), Qt::KeepAspectRatio, Qt::FastTransformation);
    
    QScreen *primaryScreen = QGuiApplication::primaryScreen();
    auto screenSize = primaryScreen->availableVirtualSize();
    
    QGraphicsScene scene; s = &scene;
    DocumentView view(&scene); v= &view;
    QObject::connect(&view, &DocumentView::fovChangedBegin, &::testBegin);
    QObject::connect(&view, &DocumentView::fovChangedEnd, &::test);
    
    view.show();
    scene.addRect(QRectF(0, 0, 100, 100));
//     scene.addPixmap(thumb);
    
    splash.showMessage("Adding it...");
     
    std::this_thread::sleep_for(2s);
    
//     SmoothPixmapView spv(pix);
//     scene.addItem(&spv);
    
    
    
    splash.finish(&view);
    
    
    
    
    
    
    struct jpeg_decompress_struct cinfo; 
    struct jpeg_error_mgr jerr;
    struct jpeg_progress_mgr progMgr;
    progMgr.progress_monitor = [](j_common_ptr cinfo) -> void
    {
        auto& p = *cinfo->progress;
        double progress = (p.completed_passes *100.) / p.total_passes;
        qWarning() << "JPEG decoding progress: " << progress << " %";
    };
    

    // Setup decompression structure
    cinfo.err = jpeg_std_error(&jerr);
    
    jpeg_create_decompress(&cinfo); 
    cinfo.progress = &progMgr;
    
    
    FILE* infile = fopen("/home/tom/Bilder/Jena/panoJenzig/AAAD2760.dpp - AAAD2902.dpp_fused_enblend.jpg", "rb");
    jpeg_stdio_src(&cinfo, infile);
  
    jpeg_read_header(&cinfo, true);
    
    v->fitInView(0,0,cinfo.image_width/2,cinfo.image_height/2, Qt::KeepAspectRatio);
    
    // set overall decompression parameters
    cinfo.buffered_image = true; /* select buffered-image mode */
    cinfo.out_color_space = JCS_EXT_BGRX;
    
    /* Used to set up image size so arrays can be allocated */
    jpeg_calc_output_dimensions(&cinfo);
    
    // Step 4: set parameters for decompression

    cinfo.dct_method = JDCT_ISLOW;
    cinfo.dither_mode = JDITHER_FS;
    cinfo.do_fancy_upsampling = true;
    cinfo.enable_2pass_quant = false;
    cinfo.do_block_smoothing = false;
    

    // Step 5: Start decompressor
    if (jpeg_start_decompress(&cinfo) == false)
    {
        qWarning() << "I/O suspension after jpeg_start_decompress()";
    }
    
    // The library's output processing will automatically call jpeg_consume_input()
    // whenever the output processing overtakes the input; thus, simple lockstep
    // display requires no direct calls to jpeg_consume_input().  But by adding
    // calls to jpeg_consume_input(), you can absorb data in advance of what is
    // being displayed.  This has two benefits:
    //   * You can limit buildup of unprocessed data in your input buffer.
    //   * You can eliminate extra display passes by paying attention to the
    //     state of the library's input processing.
//     int status;
//     do
//     {
//         status = jpeg_consume_input(&cinfo);
//     } while ((status != JPEG_SUSPENDED) && (status != JPEG_REACHED_EOI));


    switch(cinfo.output_components)
    {
        case 1:
        case 3:
        case 4:
            break;
        default:
            throw std::runtime_error("Unsupported number of pixel color components");
    }
    
    static_assert(sizeof(JSAMPLE) == sizeof(uint8_t), "JSAMPLE is not 8bits, which is unsupported");
    
    std::vector<JSAMPLE> decodedImg(cinfo.output_width * cinfo.output_height * sizeof(uint32_t));
    std::vector<JSAMPLE*> bufferSetup(cinfo.output_height);
    for(JDIMENSION i=0; i < cinfo.output_height; i++)
    {
        bufferSetup[i] = &decodedImg[i * cinfo.output_width * sizeof(uint32_t)];
    }
    
    QImage qimg(decodedImg.data(),
                cinfo.output_width,
                cinfo.output_height,
                decodedImg.size() * sizeof(JSAMPLE) / cinfo.output_height,
                QImage::Format_RGB32);
    auto preview = s->addPixmap(QPixmap::fromImage(qimg));
    
    while (!jpeg_input_complete(&cinfo))
    {
        /* start a new output pass */
        jpeg_start_output(&cinfo, cinfo.input_scan_number);
        
        auto start = std::chrono::steady_clock::now();

        while (cinfo.output_scanline < cinfo.output_height)
        {
            auto scanlinesRead = jpeg_read_scanlines(&cinfo, bufferSetup.data()+cinfo.output_scanline, cinfo.output_height);
            
            auto end = std::chrono::steady_clock::now();
            auto durationMs = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

            if(durationMs.count() > 100)
            {
                start = end;
                s->invalidate(s->sceneRect());
                QCoreApplication::processEvents();
            }
        }
        
        
        /* terminate output pass */
        jpeg_finish_output(&cinfo);
    }
    
    jpeg_finish_decompress(&cinfo);
    progMgr.completed_passes = progMgr.total_passes;
    progMgr.progress_monitor((j_common_ptr)&cinfo);
    jpeg_destroy_decompress(&cinfo);
    
    fclose(infile);
    
    
    

    
    return a.exec();
}
