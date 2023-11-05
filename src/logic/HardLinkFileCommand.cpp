
#include "HardLinkFileCommand.hpp"

#include <filesystem>

namespace fs = std::filesystem;

HardLinkFileCommand::HardLinkFileCommand(QList<QString> &&ftm, QString &&sourceFolder, QString &&destinationFolder)
    : filesToLink(std::move(ftm)), sourceFolder(std::move(sourceFolder)), destinationFolder(std::move(destinationFolder))
{
    if(this->filesToLink.size() == 1)
    {
        setText(QString("Hardlink %1 to %2")
                .arg(this->filesToLink[0])
                .arg(this->destinationFolder));
    }
    else
    {
        setText(QString("Hardlink %1 files to %2").arg(this->filesToLink.size()).arg(this->destinationFolder));
    }
}

HardLinkFileCommand::~HardLinkFileCommand() = default;

void HardLinkFileCommand::undo()
{
    static_assert(sizeof(char16_t) == sizeof(ushort), "ushort must be 2 bytes in size for this code to work");
    static_assert(alignof(char16_t) == alignof(ushort), "ushort must have same alignment as char16 for this code to work");

    fs::path targetFolder(fs::u8path(this->destinationFolder.toStdString()));
    targetFolder.make_preferred();

    QList<QPair<QString, QString>> failedLinks;

    for(auto it = this->filesToLink.begin(); it != this->filesToLink.end();)
    {
        fs::path destFileName(reinterpret_cast<const char16_t *>(it->utf16()));

        fs::path src(reinterpret_cast<const char16_t *>(this->sourceFolder.utf16()));
        src.make_preferred();
        src /= destFileName;

        fs::path dest(targetFolder);
        dest /= destFileName;

        if(!fs::exists(dest))
        {
            failedLinks.append(QPair<QString, QString>(*it, QString("Destination no longer exists.")));
            it = this->filesToLink.erase(it);
        }
        else if(!fs::equivalent(src, dest))
        {
            failedLinks.append(QPair<QString, QString>(*it, QString("The previously created hardlink is no longer equivalent to the former source file.")));
            it = this->filesToLink.erase(it);
        }
        else
        {
            try
            {
                fs::remove(dest);
                ++it;
            }
            catch(const fs::filesystem_error &e)
            {
                failedLinks.append(QPair<QString, QString>(*it, e.what()));
                it = this->filesToLink.erase(it);
            }
        }
    }

    if(!failedLinks.empty())
    {
        emit failed(failedLinks);
    }

    if(this->filesToLink.empty())
    {
        this->setObsolete(true);
    }
    else
    {
        emit succeeded(this->filesToLink);
    }
}

void HardLinkFileCommand::redo()
{
    this->doLink(this->sourceFolder, this->destinationFolder);
}

void HardLinkFileCommand::doLink(const QString &sourceFolder, const QString &destinationFolder)
{
    static_assert(sizeof(char16_t) == sizeof(ushort), "ushort must be 2 bytes in size for this code to work");
    static_assert(alignof(char16_t) == alignof(ushort), "ushort must have same alignment as char16 for this code to work");

    fs::path targetFolder(fs::u8path(destinationFolder.toStdString()));
    targetFolder.make_preferred();

    QList<QPair<QString, QString>> failedLinks;

    for(auto it = this->filesToLink.begin(); it != this->filesToLink.end();)
    {
        fs::path destFileName(reinterpret_cast<const char16_t *>(it->utf16()));

        fs::path src(reinterpret_cast<const char16_t *>(sourceFolder.utf16()));
        src.make_preferred();
        src /= destFileName;

        fs::path dest(targetFolder);
        dest /= destFileName;

        if(!fs::exists(src))
        {
            failedLinks.append(QPair<QString, QString>(*it, QString("Source vanished.")));
            it = this->filesToLink.erase(it);
        }
        else if(fs::exists(dest))
        {
            failedLinks.append(QPair<QString, QString>(*it, QString("Destination already exists.")));
            it = this->filesToLink.erase(it);
        }
        else
        {
            try
            {
                if(!fs::is_regular_file(src))
                {
                    throw std::runtime_error("Refusing to hardlink non-regular file.");
                }

                fs::create_hard_link(src, dest);
                ++it;
            }
            catch(const std::runtime_error &e)
            {
                failedLinks.append(QPair<QString, QString>(*it, e.what()));
                it = this->filesToLink.erase(it);
            }
        }
    }

    if(!failedLinks.empty())
    {
        emit failed(failedLinks);
    }

    if(this->filesToLink.empty())
    {
        this->setObsolete(true);
    }
    else
    {
        emit succeeded(this->filesToLink);
    }
}
