
#include "MoveFileCommand.hpp"

#include <filesystem>

namespace fs = std::filesystem;

MoveFileCommand::MoveFileCommand(QList<QString> &&ftm, QString &&sourceFolder, QString &&destinationFolder)
    : filesToMove(std::move(ftm)), sourceFolder(std::move(sourceFolder)), destinationFolder(std::move(destinationFolder))
{
    if(filesToMove.size() == 1)
    {
        setText(QString("Move %1 to %2")
                .arg(filesToMove[0])
                .arg(this->destinationFolder));
    }
    else
    {
        setText(QString("Move %1 files to %2").arg(filesToMove.size()).arg(this->destinationFolder));
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

void MoveFileCommand::doMove(const QString &sourceFolder, const QString &destinationFolder)
{
    static_assert(sizeof(char16_t) == sizeof(ushort), "ushort must be 2 bytes in size for this code to work");
    static_assert(alignof(char16_t) == alignof(ushort), "ushort must have same alignment as char16 for this code to work");

    fs::path targetFolder(fs::u8path(destinationFolder.toStdString()));
    targetFolder.make_preferred();

    QList<QPair<QString, QString>> failedMoves;

    for(auto it = filesToMove.begin(); it != filesToMove.end();)
    {
        fs::path destFileName(reinterpret_cast<const char16_t *>(it->utf16()));

        fs::path src(reinterpret_cast<const char16_t *>(sourceFolder.utf16()));
        src.make_preferred();
        src /= destFileName;

        fs::path dest(targetFolder);
        dest /= destFileName;

        if(!fs::exists(src))
        {
            failedMoves.append(QPair<QString, QString>(*it, QString("Source vanished.")));
            it = filesToMove.erase(it);
        }
        else if(fs::exists(dest))
        {
            failedMoves.append(QPair<QString, QString>(*it, QString("Destination already exists.")));
            it = filesToMove.erase(it);
        }
        else
        {
            try
            {
                fs::rename(src, dest);
                ++it;
            }
            catch(const fs::filesystem_error &e)
            {
                failedMoves.append(QPair<QString, QString>(*it, e.what()));
                it = filesToMove.erase(it);
            }
        }
    }

    if(!failedMoves.empty())
    {
        emit failed(failedMoves);
    }

    if(filesToMove.empty())
    {
        this->setObsolete(true);
    }
    else
    {
        emit succeeded(filesToMove);
    }
}
