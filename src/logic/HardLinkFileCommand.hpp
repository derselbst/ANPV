
#pragma once

#include <QUndoCommand>
#include <QString>
#include <QList>

class HardLinkFileCommand : public QObject, public QUndoCommand
{
    Q_OBJECT
    
    QList<QString> filesToLink;
    QString sourceFolder;
    QString destinationFolder;
    
    void doLink(const QString& sourceFolder, const QString& destinationFolder);
    
public:
    HardLinkFileCommand(QList<QString>&& ftm, QString&& sourceFolder, QString&& destinationFolder);
    ~HardLinkFileCommand() override;
    
    void undo() override;

    void redo() override;
    
    
signals:
    void failed(QList<QPair<QString, QString>>);
    void succeeded(QList<QString>);
};
