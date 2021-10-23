#include "MultiDocumentView.hpp"


struct MultiDocumentView::Impl
{
    MultiDocumentView* q;
    
};

MultiDocumentView::MultiDocumentView(QWidget *parent)
 : QTabWidget(parent), d(std::make_unique<Impl>(this))
{
}

MultiDocumentView::~MultiDocumentView() = default;
