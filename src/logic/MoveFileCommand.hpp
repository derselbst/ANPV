
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

    void doMove(const QString &sourceFolder, const QString &destinationFolder);

public:
    MoveFileCommand(QList<QString> &&ftm, QString &&sourceFolder, QString &&destinationFolder);
    ~MoveFileCommand() override;

    void undo() override;

    void redo() override;


signals:
    void failed(QList<QPair<QString, QString>>);
    void succeeded(QList<QString>);
};
