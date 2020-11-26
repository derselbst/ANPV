
enum Column : int
{
	FileName,
	FileSize,
	Resolution,
	DateRecorded,
	Aperture,
	Exposure,
	Iso,
	FocalLength,
	Lens,
	CameraModel,
	Count // must be last!
};

struct Entry
{
	bool hasImageDecoder;

	union
	{
		std::unique_ptr<SmartImageDecoder> dec;
		QFileInfo info;
	};

	QFileInfo getFileInfo()
	{
		return this->hasImageDecoder ? this->dec->fileInfo() : this->info;
	}

	SmartImageDecoder* getDecoder()
	{
		return this->hasImageDecoder ? this->dec.get() : nullptr;
	}
};

struct OrderedFileSystemModel::Impl
{
	std::atomic<bool> directoryLoadingCancelled{ false };

	QFuture<void> directoryWorker;

	std::vector<Entry> entries;
	
	// The column which is currently sorted
	Column currentSortedCol;
	Qt::SortOrder sortOrder;

	// returns true if the column that is sorted against requires us to preload the image metadata
	// before we insert the items into the model
	bool sortedColumnNeedsPreloadingMetadata()
	{
		if (currentSortCol == Column::FileName || currentSortedCol == Column::FileSize)
		{
			return false;
		}
		else
		{
			return true;
		}
	}

	static void throwIfDirectoryLoadingCancelled(void* self)
	{
		if (static_cast<Impl*>(self)->directoryLoadingCancelled)
		{
			throw UserCancellation();
		}
	}

	template<Column SortCol>
	static bool sortColumnPredicateLeftBeforeRight(const Entry& l, const QFileInfo& linfo, const Entry& r, const QFileInfo& r)
	{
		auto* ldec = l.getDecoder();
		auto* rdec = r.getDecoder();

		if (ldec && rdec)
		{
			if constexpr (SortCol == Column::Resolution)
			{
				QSize lsize = ldec->size();
				QSize rsize = rdec->size();

				if (lsize.width() != rsize.width() && lsize.height() != rsize.height())
				{
					return lsize.width() * lsize.height() < rsize.width() * rsize.height();
				}
			}
			else if constexpr (SortCol == Column::DateRecorded)
			{
			}
			else if constexpr (SortCol == Column::Aperture)
			{
			}
			else if constexpr (SortCol == Column::Exposure)
			{
			}
			else if constexpr (SortCol == Column::Iso)
			{
			}
			else if constexpr (SortCol == Column::FocalLength)
			{
			}
			else if constexpr (SortCol == Column::Lens)
			{
			}
			else if constexpr (SortCol == Column::CameraModel)
			{
			}
			else if constexpr (SortCol == Column::FileName)
			{
				// nothing to do here, we use the fileName comparison below
			}
			else if constexpr (SortCol == Column::FileSize)
			{
				return linfo.size() < rinfo.size();
			}
			else
			{
				static_assert("Unknown column to sort for", !sizeof(char));
			}
		}
		else if (ldec && !rdec)
		{
			return true; // l before r
		}
		else if (!ldec && rdec)
		{
			return false; // l behind r
		}

		return linfo.fileName() < rinfo.fileName();
	}

	// This is the entry point for sorting. It sorts all Directories first.
	// Second criteria is to sort according to fileName
	// For regular files it dispatches the call to sortColumnPredicateLeftBeforeRight()
	//
	// |   L  \   R    | DIR  | SortCol | UNKNOWN |
	// |      DIR      |  1   |   1     |    1    |
	// |     SortCol   |  0   |   1     |    1    |
	// |    UNKNOWN    |  0   |   0     |    1    |
	//
	template<Column SortCol>
	static bool topLevelSortFunction(const Entry& l, const Entry& r)
	{
		QFileInfo linfo = l.getFileInfo();
		QFileInfo rinfo = r.getFileInfo();

		bool leftIsBeforeRight =
			linfo.isDir() && linfo.fileName() < rinfo.fileName() ||
			!rinfo.isDir() && sortColumnPredicateLeftBeforeRight<SortCol>(l, linfo, r, rinfo);
		
		return leftIsBeforeRight;
	}

	auto getSortFunction()
	{
		switch (d->currentSortedCol)
		{
		case Column::FileName:
			return [](const Entry& l, const Entry& r) { return topLevelSortFunction<Column::FileName>(l, r); };
		
		case Column::FileSize:
			return [](const Entry& l, const Entry& r) { return topLevelSortFunction<Column::FileSize>(l, r); };
			
		case Column::Resolution:
			return [](const Entry& l, const Entry& r) { return topLevelSortFunction<Column::Resolution>(l, r); };

		case Column::DateRecorded:
			return [](const Entry& l, const Entry& r) { return topLevelSortFunction<Column::DateRecorded>(l, r); };

		case Column::Aperture:
			return [](const Entry& l, const Entry& r) { return topLevelSortFunction<Column::Aperture>(l, r); };

		case Column::Exposure:
			return [](const Entry& l, const Entry& r) { return topLevelSortFunction<Column::Exposure>(l, r); };

		case Column::Iso:
			return [](const Entry& l, const Entry& r) { return topLevelSortFunction<Column::Iso>(l, r); };

		case Column::FocalLength:
			return [](const Entry& l, const Entry& r) { return topLevelSortFunction<Column::FocalLength>(l, r); };

		case Column::Lens:
			return [](const Entry& l, const Entry& r) { return topLevelSortFunction<Column::Lens>(l, r); };

		case Column::CameraModel:
			return [](const Entry& l, const Entry& r) { return topLevelSortFunction<Column::CameraModel>(l, r); };

		default:
			throw std::logic_error(Formatter() << "No sorting function implemented for column " << d->currentSortCol);
		}
	}

	void sortEntries()
	{
		auto sortFunction = getSortFunction();
		std::sort(std::execution::par_unseq, std::begin(d->entries), std::end(d->entries), sortFunction);
	}

	void onDirectoryLoaded()
	{
		q->beginInsertRows(QModelIndex(), 0, d->entries.size());
		q->endInsertRows();
	}

	void startImageDecoding(const SmartImageDecoder& dec, DecodingState targetState)
	{

	}
};


void VIEW::onDirectoryLoadingProgress(int progress)
{

}

void OrderedFileSystemModel::clear()
{
	this->beginResetModel();

	d->currentDir = QDir();
	d->entries.clear();
	d->entries.shrink_to_fit();

	this->endResetModel();
}

void OrderedFileSystemModel::changeDirAsync(const QDir& dir)
{
	d->directoryLoadingCancelled = true;
	d->setStatusMessage("Waiting for previous directory parsing to finish...");
	d->directoryWorker.waitForFinished();
	d->directoryLoadingCancelled = false;

	d->clear();
	d->currentDir = dir;

	d->setStatusMessage("Loading Directory Entries");

	d->directoryWorker = QtConcurrent::run(QThreadPool::globalInstance(),
		[&]()
		{
			try
			{
				QFileInfoList fileInfoList = d->currentDir.entryInfoList(QDir::AllEntries | QDir::NoDot);

				const auto entriesToProcess = fileInfoList.size();
				decltype(entriesToProcess) entriesProcessed = 0;
				d->entries.reserve(entriesToProcess);
				while (!fileInfoList.isEmpty())
				{
					do
					{
						QFileInfo inf = list.takeFirst();
						if (inf.isFile())
						{
							auto decoder = DecoderFactory::load(inf.absoluteFilePath());
							if (decoder)
							{
								decoder->setCancellationCallback(&OrderedFileSystemModel::Impl::throwIfDirectoryLoadingCancelled, d.get());

								if (d->sortedColumnNeedsPreloadingMetadata())
								{
									decoder.decode(DecodingState::Metadata);
								}

								d->entries.emplace_back({});
								Entry& e = d->entries.back();
								e.hasImageDecoder = true;
								e.dec = std::move(decoder);

								break;
							}
						}

						d->entries.emplace_back(Entry());
						Entry& e = d->entries.back();
						e.hasImageDecoder = false;
						e.info = std::move(inf);

					} while (false);

					d->throwIfDirectoryLoadingCancelled(d.get());
					emit directoryLoadingProgress(++entriesProcessed * 100. / entriesToProcess)
				}

				d->sortEntries();
				emit directoryLoaded();
			}
			catch (const UserCancellation&)
			{
				qInfo() << "Directory loading cancelled";
			}
			catch (const std::exception& e)
			{
				emit directoryLoadingFailed("Fatal error occurred while loading the directory.", e.what());
			}
		});
}




int OrderedFileSystemModel::columnCount(const QModelIndex &) const
{
	return Column::Count;
}

int OrderedFileSystemModel::rowCount(const QModelIndex&) const
{
	if (d->directoryWorker.isFinished())
	{
		return d->entries.size();
	}
	else
	{
		return 0;
	}
}

QVariant QAbstractItemModel::data(const QModelIndex& index, int role) const
{
	if (index.isValid())
	{
		QFileIconProvider iconProvider;
		const Entry* e = d->entries.at(index.row());

		QFileInfo fileInfo;
		if (e->hasImageDecoder)
		{
			fileInfo = e->dec->fileInfo();
		}
		else
		{
			fileInfo = e->info;
		}

		switch (role)
		{
		case Qt::DisplayRole:
			return fileInfo.fileName();

		case Qt::DecorationRole:
			if (d->hasImageDecoder)
			{
				switch (e->dec->decodingState())
				{
				case DecodingState::Ready:
					trigger e->dec->decode(DecodingState::Metadata);
					break;

				case DecodingState::Error:
					return this->style()->standardIcon(QStyle::SP_MessageBoxCritical);

				case DecodingState::Metadata:
					QPixmap thumbnail = d->dec->thumbnail();
					if (!thumbnail.isNull())
					{
						return thumbnail;
					}
					else
					{
						trigger e->dec->decode(DecodingState::PreviewImage);
						break;
					}
				case DecodingState::PreviewImage:
				case DecodingState::FullImage:
					QPixmap deepCopyImage = d->dec->image().scaled(500, 500, Qt::KeepAspectRatio, Qt::SmoothTransformation);
					return deepCopyImage;
				}
			}
			return iconProvider.icon(fileInfo);

		case Qt::TextAlignmentRole:
			if (index.column() == Column::FileName)
			{
				const Qt::Alignment alignment = Qt::AlignRight | Qt::AlignVCenter;
				return int(alignment);
			}

		case Qt::ToolTipRole:
			if (d->hasImageDecoder)
			{
				switch (e->dec->decodingState())
				{
				case DecodingState::Error:
					return e->dec->errorMessage();
				default:
					break;
				}
			}
			return QVariant();

		case Qt::EditRole:
		case Qt::StatusTipRole:
		case Qt::WhatsThisRole:
		default:
			return QVariant();
		}
	}
}

bool OrderedFileSystemModel::insertRows(int row, int count, const QModelIndex& parent)
{
	return false;

	this->beginInsertRows(parent, row, row + count - 1);

	this->endInsertRows();
}

void QAbstractItemModel::sort(int column, Qt::SortOrder order = Qt::AscendingOrder)
{
	if (order == Qt::DescendingOrder)
	{
		qWarning() << "Descending sort order not supported yet";
	}

	d->currentSortedCol = static_cast<Column>(column);
	d->sortOrder = order;
	d->sortEntries();
}