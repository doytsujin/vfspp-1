
#include <VFSPP/merged.hpp>

#include <boost/algorithm/string.hpp>

#include <boost/foreach.hpp>

using namespace vfspp;
using namespace vfspp::merged;

using namespace boost;

typedef unordered_map<string_type, shared_ptr<MergedEntry> >::iterator ChildMapping;

MergedEntry::MergedEntry(MergedFileSystem* parentSystem, shared_ptr<IFileSystemEntry> contained) :
IFileSystemEntry(contained ? contained->getPath() : ""), parentSystem(parentSystem), containedEntry(contained),
dirty(true)
{
}

void MergedEntry::addChildren(IFileSystemEntry* entry)
{
	if (entry->getType() != DIRECTORY)
	{
		return;
	}

	std::vector<shared_ptr<IFileSystemEntry> > entries;

	entry->listChildren(entries);

	BOOST_FOREACH(shared_ptr<IFileSystemEntry>& childEntry, entries)
	{
		if (cachedChildMapping.find(childEntry->getPath()) == cachedChildMapping.end())
		{
			// This entry hasn't been found yet
			shared_ptr<MergedEntry> newEntry(new MergedEntry(parentSystem, childEntry));

			cachedChildMapping.insert(std::make_pair(childEntry->getPath(), newEntry));
			cachedChildEntries.push_back(newEntry);
		}
	}
}

void MergedEntry::cacheChildren()
{
	cachedChildEntries.clear();
	cachedChildMapping.clear();

	BOOST_FOREACH(shared_ptr<IFileSystem>& system, parentSystem->fileSystems)
	{
		if (system->supportedOperations() & OP_READ)
		{
			if (isRoot())
			{
				addChildren(system->getRootEntry());
			}
			else
			{
				shared_ptr<IFileSystemEntry> entry = system->getRootEntry()->getChild(path);

				if (entry)
				{
					addChildren(entry.get());
				}
			}
		}
	}

	dirty = false;
}

boost::shared_ptr<MergedEntry> MergedEntry::getEntryInternal(const string_type& path)
{
	size_t separator = path.find_first_of(DirectorySeparatorChar);

	if (separator == string_type::npos)
	{
		ChildMapping found = cachedChildMapping.find(path);

		if (found != cachedChildMapping.end())
		{
			return found->second;
		}
	}
	else
	{
		string_type thisLevel = path.substr(0, separator);

		ChildMapping found = cachedChildMapping.find(thisLevel);

		if (found != cachedChildMapping.end())
		{
			return found->second->getEntryInternal(path.substr(separator + 1));
		}
	}

	return shared_ptr<MergedEntry>();
}

boost::shared_ptr<IFileSystemEntry> MergedEntry::getChild(const string_type& path)
{
	if (getType() != DIRECTORY)
	{
		throw InvalidOperationException("Entry is no directory!");
	}

	if (dirty)
	{
		cacheChildren();
	}

	return getEntryInternal(normalizePath(path));
}

size_t MergedEntry::numChildren()
{
	if (getType() != DIRECTORY)
	{
		throw InvalidOperationException("Entry is no directory!");
	}

	if (dirty)
	{
		cacheChildren();
	}

	return cachedChildEntries.size();
}

void MergedEntry::listChildren(std::vector<boost::shared_ptr<IFileSystemEntry> >& outVector)
{
	if (getType() != DIRECTORY)
	{
		throw InvalidOperationException("Entry is no directory!");
	}

	if (dirty)
	{
		cacheChildren();
	}

	outVector.clear();

	std::copy(cachedChildEntries.begin(), cachedChildEntries.end(), std::back_inserter(outVector));
}

boost::shared_ptr<std::streambuf> MergedEntry::open(int mode)
{
	int ops = modeToOperation(mode);

	if (!(parentSystem->supportedOperations() & ops))
	{
		throw InvalidOperationException("No filesystem supports the requested operations!");
	}

	if (getType() != FILE)
	{
		throw InvalidOperationException("Entry is no file!");
	}

	BOOST_FOREACH(shared_ptr<IFileSystem>& system, parentSystem->fileSystems)
	{
		if (system->supportedOperations() & ops)
		{
			shared_ptr<IFileSystemEntry> entry = system->getRootEntry()->getChild(path);

			if (entry && entry->getType() == FILE)
			{
				try
				{
					shared_ptr<std::streambuf> buffer = entry->open(mode);

					if (buffer)
					{
						// Stop if we have sucessfully opened a file
						return buffer;
					}
				}
				catch (const FileSystemException&)
				{
					// Ignore filesystem errors and continue searching
				}
			}
		}
	}

	throw FileSystemException("Failed to open file from any filesystem!");
}

EntryType MergedEntry::getType() const
{
	if (!isRoot())
	{
		return containedEntry->getType();
	}
	else
	{
		// Root directory
		return DIRECTORY;
	}
}

bool MergedEntry::deleteChild(const string_type& name)
{
	if (!(parentSystem->supportedOperations() & OP_DELETE))
	{
		throw InvalidOperationException("No filesystem supports deleting!");
	}

	if (getType() != DIRECTORY)
	{
		throw InvalidOperationException("Entry is no directory!");
	}

	bool success = false;
	BOOST_FOREACH(shared_ptr<IFileSystem>& system, parentSystem->fileSystems)
	{
		if (system->supportedOperations() & OP_DELETE)
		{
			shared_ptr<IFileSystemEntry> entry = system->getRootEntry()->getChild(path);

			if (entry && entry->getType() == DIRECTORY)
			{
				try
				{
					success = entry->deleteChild(name) || success;
				}
				catch (const FileSystemException&)
				{
					// Ignore filesystem errors and continue searching
				}
			}
		}
	}

	return success;
}

boost::shared_ptr<IFileSystemEntry> MergedEntry::createEntry(EntryType type, const string_type& name)
{
	if (!(parentSystem->supportedOperations() & OP_CREATE))
	{
		throw InvalidOperationException("No filesystem supports deleting!");
	}

	if (getType() != DIRECTORY)
	{
		throw InvalidOperationException("Entry is no directory!");
	}

	BOOST_FOREACH(shared_ptr<IFileSystem>& system, parentSystem->fileSystems)
	{
		if (system->supportedOperations() & OP_CREATE)
		{
			shared_ptr<IFileSystemEntry> entry = system->getRootEntry()->getChild(path);

			if (entry && entry->getType() == DIRECTORY)
			{
				try
				{
					shared_ptr<IFileSystemEntry> newEntry = entry->createEntry(type, name);

					if (newEntry)
					{
						// Stop if we have sucessfully created an entry
						return shared_ptr<MergedEntry>(new MergedEntry(parentSystem, newEntry));
					}
				}
				catch (const FileSystemException&)
				{
					// Ignore filesystem errors and continue searching
				}
			}
		}
	}

	return shared_ptr<IFileSystemEntry>();
}