
#include "DeleteFileCommand.hpp"

#include <QDir>
#include <filesystem>

namespace fs = std::filesystem;

DeleteFileCommand::DeleteFileCommand(QList<QString> &&files, QString &&sourceFolder)
    : filesToDelete(std::move(files)), sourceFolder(std::move(sourceFolder))
{
    if(filesToDelete.size() == 1)
    {
        this->setText(QString("Delete %1")
                      .arg(filesToDelete[0]));
    }
    else
    {
        this->setText(QString("Delete %1 files").arg(filesToDelete.size()));
    }
    // Win32 long filename hack \\?\ does not apply here! Because it's Qt and not std::filesystem
}

DeleteFileCommand::~DeleteFileCommand() = default;

void DeleteFileCommand::undo()
{
    QList<QPair<QString, QString>> failedRestores;

    QDir restoreDir(sourceFolder);

    if(trashFilePaths.size() != filesToDelete.size())
    {
        throw std::logic_error("Oops, these lists should have equal size");
    }

    auto itTrash = trashFilePaths.begin();
    auto itOrig = filesToDelete.begin();

    for(; itTrash != trashFilePaths.end();)
    {
        QString origAbsFilePath(restoreDir.absoluteFilePath(*itOrig));

        if(QFileInfo(origAbsFilePath).exists())
        {
            failedRestores.append({ *itOrig, "A file at the restore location seems to exist already, refusing to overwrite that" });
            itTrash = trashFilePaths.erase(itTrash);
            itOrig = filesToDelete.erase(itOrig);
            continue;
        }

        bool success = QFile::rename(*itTrash, origAbsFilePath);

        if(!success)
        {
            failedRestores.append({ *itOrig, "Unspecified error while restoring the file" });
            itTrash = trashFilePaths.erase(itTrash);
            itOrig = filesToDelete.erase(itOrig);
            continue;
        }

        itTrash = trashFilePaths.erase(itTrash);
        itOrig++;
    }

    Q_ASSERT(trashFilePaths.size() == 0);

    if(!failedRestores.empty())
    {
        emit failed(failedRestores);
    }

    if(filesToDelete.empty())
    {
        this->setObsolete(true);
    }
    else
    {
        emit succeeded(filesToDelete);
    }
}

void DeleteFileCommand::redo()
{
    QList<QPair<QString, QString>> failedDels;

    Q_ASSERT(trashFilePaths.size() == 0);

    QDir sourceDir(sourceFolder);

    for(auto it = filesToDelete.begin(); it != filesToDelete.end();)
    {
        QString absoluteFilePath = sourceDir.absoluteFilePath(*it);
        QFile file(absoluteFilePath);

        if(!file.exists())
        {
            failedDels.append({ *it, "does not exist" });
            it = filesToDelete.erase(it);
            continue;
        }

        bool success = file.moveToTrash();

        if(!success || QFile::exists(absoluteFilePath))
        {
            failedDels.append({ *it, "deletion failed, file might be currently in use" });
            it = filesToDelete.erase(it);
            continue;
        }

        this->trashFilePaths.append(QFileInfo(file).absoluteFilePath());
        it++;
    }

    if(!failedDels.empty())
    {
        emit failed(failedDels);
    }

    if(filesToDelete.empty())
    {
        this->setObsolete(true);
    }
    else
    {
        emit succeeded(filesToDelete);
    }
}

