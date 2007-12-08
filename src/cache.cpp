/*
	Description: file names persistent cache

	Author: Marco Costalba (C) 2005-2007

	Copyright: See COPYING file that comes with this distribution

*/
#include <QFile>
#include <QDir>
#include "cache.h"

using namespace QGit;

bool Cache::save(const QString& gitDir, const RevFileMap& rf,
                 const StrVect& dirs, const StrVect& files) {

	if (gitDir.isEmpty() || rf.isEmpty())
		return false;

	QString path(gitDir + C_DAT_FILE);
	QString tmpPath(path + BAK_EXT);

	QDir dir;
	if (!dir.exists(gitDir)) {
		dbs("Git directory not found, unable to save cache");
		return false;
	}
	QFile f(tmpPath);
	if (!f.open(QIODevice::WriteOnly))
		return false;

	dbs("Saving cache. Please wait...");

	// compress in memory before write to file
	QByteArray data;
	QDataStream stream(&data, QIODevice::WriteOnly);

	// Write a header with a "magic number" and a version
	stream << (quint32)C_MAGIC;
	stream << (qint32)C_VERSION;

	stream << (qint32)dirs.count();
	for (int i = 0; i < dirs.count(); ++i)
		stream << dirs.at(i);

	stream << (qint32)files.count();
	for (int i = 0; i < files.count(); ++i)
		stream << files.at(i);

	// to achieve a better compression we save the sha's as
	// one very long string instead of feeding the stream with
	// each one. With this trick we gain a 15% size reduction
	// in the final compressed file. The save/load speed is
	// almost the same.
	uint bufSize = rf.count() * 40 + 1000; // a little bit more space then required
	stream << (qint32)bufSize;

	QString buf;
	buf.reserve(bufSize);
	FOREACH (RevFileMap, it, rf) {

		SCRef sha = it.key();
		if (   sha == ZERO_SHA
		    || sha == CUSTOM_SHA
		    || sha.startsWith("A")) // ALL_MERGE_FILES + rev sha
			continue;

		buf.append(sha);
	}
	stream << buf;

	FOREACH (RevFileMap, it, rf) {

		SCRef sha = it.key();
		if (   sha == ZERO_SHA
		    || sha == CUSTOM_SHA
		    || sha.startsWith("A")) // ALL_MERGE_FILES + rev sha
			continue;

		const RevFile* rf = it.value();
		stream << rf->names;
		stream << rf->dirs;

		// skip common case of only modified files
		bool isEmpty = rf->onlyModified;
		stream << (quint32)isEmpty;
		if (!isEmpty)
			stream << rf->status;

		// skip common case of just one parent
		isEmpty = (rf->mergeParent.isEmpty() || rf->mergeParent.last() == 1);
		stream << (quint32)isEmpty;
		if (!isEmpty)
			stream << rf->mergeParent;

		// skip common case of no rename/copies
		isEmpty = rf->extStatus.isEmpty();
		stream << (quint32)isEmpty;
		if (!isEmpty)
			stream << rf->extStatus;
	}
	dbs("Compressing data...");
	f.write(qCompress(data, 1)); // no need to encode with compressed data
	f.close();

	// rename C_DAT_FILE + BAK_EXT -> C_DAT_FILE
	if (dir.exists(path)) {
		if (!dir.remove(path)) {
			dbs("access denied to " + path);
			dir.remove(tmpPath);
			return false;
		}
	}
	dir.rename(tmpPath, path);
	dbs("Done.");
	return true;
}

bool Cache::load(const QString& gitDir, RevFileMap& rfm, StrVect& dirs, StrVect& files) {

	// check for cache file
	QString path(gitDir + C_DAT_FILE);
	QFile f(path);
	if (!f.exists())
		return true; // no cache file is not an error

	if (!f.open(QIODevice::ReadOnly))
		return false;

	QDataStream* stream = new QDataStream(qUncompress(f.readAll()));
	quint32 magic;
	qint32 version;
	qint32 dirsNum, filesNum, bufSize;
	*stream >> magic;
	*stream >> version;
	if (magic != C_MAGIC || version != C_VERSION) {
		f.close();
		delete stream;
		return false;
	}
	// read the data
	*stream >> dirsNum;
	dirs.resize(dirsNum);
	for (int i = 0; i < dirsNum; ++i)
		*stream >> dirs[i];

	*stream >> filesNum;
	files.resize(filesNum);
	for (int i = 0; i < filesNum; ++i)
		*stream >> files[i];

	*stream >> bufSize;
	QString buf;
	buf.reserve(bufSize);
	*stream >> buf;

	uint bufIdx = 0;
	bool isEmpty;
	quint32 tmp;
	while (!stream->atEnd()) {

		RevFile* rf = new RevFile();
		*stream >> rf->names;
		*stream >> rf->dirs;

		*stream >> tmp;
		rf->onlyModified = (bool)tmp;
		if (!rf->onlyModified)
			*stream >> rf->status;

		*stream >> tmp;
		isEmpty = (bool)tmp;
		if (!isEmpty)
			*stream >> rf->mergeParent;

		*stream >> tmp;
		isEmpty = (bool)tmp;
		if (!isEmpty)
			*stream >> rf->extStatus;

		SCRef sha(buf.mid(bufIdx, 40));
		rfm.insert(sha, rf);
		bufIdx += 40;
	}
	f.close();
	delete stream;
	return true;
}
