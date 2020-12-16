
#pragma once

#include <QUndoCommand>
#include <QString>
#include <QList>

class MoveFileCommand : public QObject, public QUndoCommand
{
    Q_OBJECT
    
    QList<QString> filesToMove;
    QString sourceFolder;
    QString destinationFolder;
    
    void doMove(const QString& sourceFolder, const QString& destinationFolder);
    
public:
    MoveFileCommand(const QList<QString>& filesToMove, const QString& sourceFolder, const QString &destinationFolder);
    ~MoveFileCommand() override;
    
    void undo() override;

    void redo() override;
    
    
signals:
    void moveFailed(QList<QPair<QString, QString>>);
    void moveSucceeded(QList<QString>);
};
