
#pragma once

#include <QUndoCommand>
#include <QString>
#include <QList>

class DeleteFileCommand : public QObject, public QUndoCommand
{
    Q_OBJECT
    
    QList<QString> filesToDelete;
    QString sourceFolder;
	
	QList<QString> trashFilePaths;
    
    void doDelete(const QString& sourceFolder);
    
public:
    DeleteFileCommand(QList<QString>&& ftm, QString&& sourceFolder);
    ~DeleteFileCommand() override;
    
    void undo() override;

    void redo() override;
    
    
signals:
    void failed(QList<QPair<QString, QString>>);
    void succeeded(QList<QString>);
};
