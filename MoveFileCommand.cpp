
#include "MoveFileCommand.hpp"

#include <experimental/filesystem>

namespace fs = std::experimental::filesystem;

MoveFileCommand::MoveFileCommand(const QList<QString>& filesToMove, const QString& sourceFolder, const QString &destinationFolder)
    : filesToMove(filesToMove), sourceFolder(sourceFolder), destinationFolder(destinationFolder)
    {
        if(filesToMove.size() == 1)
        {
            setText(QString("Move %1 to %2")
            .arg(filesToMove[0])
            .arg(destinationFolder));
        }
        else
        {
            setText(QString("Move %1 files to %2").arg(filesToMove.size()).arg(destinationFolder));
        }
    }

MoveFileCommand::~MoveFileCommand() = default;

void MoveFileCommand::undo()
{
    this->doMove(this->destinationFolder, this->sourceFolder);
}

void MoveFileCommand::redo()
{
    this->doMove(this->sourceFolder, this->destinationFolder);
}

void MoveFileCommand::doMove(const QString& sourceFolder, const QString& destinationFolder)
{
    const fs::path targetFolder(destinationFolder.toStdString());
    
    QList<QPair<QString, QString>> failedMoves;
    
    for(auto it=filesToMove.begin(); it != filesToMove.end(); )
    {
        fs::path src(sourceFolder.toStdString());
        src /= it->toStdString();
        
        fs::path dest(targetFolder);
        dest /= it->toStdString();
        
        if(!fs::exists(src))
        {
            failedMoves.append(QPair<QString,QString>(*it, QString("Source vanished.")));
            it = filesToMove.erase(it);
        }
        else if(fs::exists(dest))
        {
            failedMoves.append(QPair<QString,QString>(*it, QString("Destination already exists.")));
            it = filesToMove.erase(it);
        }
        else
        {
            try
            {
                fs::rename(src, dest);
                ++it;
            }
            catch(const fs::filesystem_error& e)
            {
                failedMoves.append(QPair<QString,QString>(*it, e.what()));
                it = filesToMove.erase(it);
            }
        }
    }
    
    if(!failedMoves.empty())
    {
        emit moveFailed(failedMoves);
    }
    
    if(filesToMove.empty())
    {
        this->setObsolete(true);
    }
    else
    {
        emit moveSucceeded(filesToMove);
    }
}
