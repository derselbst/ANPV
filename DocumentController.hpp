
#pragma once

#include "DecodingState.hpp"

#include <QObject>
#include <memory>

class SmartImageDecoder;
class DocumentView;

class DocumentController : public QObject
{
Q_OBJECT

public:
    DocumentController(QObject *parent = nullptr);
    ~DocumentController() override;

    DocumentView* documentView();

public slots:
    void onBeginFovChanged();
    void onEndFovChanged();
    void onDecodingStateChanged(SmartImageDecoder* self, quint32 newState, quint32 oldState);
    void onDecodingProgress(SmartImageDecoder* self, int progress, QString message);
    
private:
    struct Impl;
    std::unique_ptr<Impl> d;
};
