/*
 * Copyright 2016 Matthieu Gallien <matthieu_gallien@yahoo.fr>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 3 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public License
 * along with this library; see the file COPYING.LIB.  If not, write to
 * the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#include "databaseinterface.h"

#include <QtSql/QSqlDatabase>
#include <QtSql/QSqlQuery>
#include <QtSql/QSqlRecord>
#include <QtSql/QSqlError>

#include <QtCore/QVariant>
#include <QtCore/QDebug>

#include <algorithm>

class DatabaseInterfacePrivate
{
public:

    QSqlDatabase mTracksDatabase;

    QVector<qlonglong> mIndexByPosition;

    QHash<qlonglong, int> mPositionByIndex;

    QHash<qlonglong, MusicAlbum> mAlbumCache;

};

DatabaseInterface::DatabaseInterface(QObject *parent) : QObject(parent), d(new DatabaseInterfacePrivate)
{
    static int databaseCounter = 1;
    d->mTracksDatabase = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), QStringLiteral("album%1").arg(databaseCounter));
    ++databaseCounter;
    d->mTracksDatabase.setDatabaseName(QStringLiteral("file:memdb1?mode=memory&cache=shared"));
    d->mTracksDatabase.setConnectOptions(QStringLiteral("foreign_keys = ON;QSQLITE_OPEN_URI;QSQLITE_ENABLE_SHARED_CACHE"));
    auto result = d->mTracksDatabase.open();
    if (result) {
        qDebug() << "database open";
    } else {
        qDebug() << "database not open";
    }

    initDatabase();
}

DatabaseInterface::~DatabaseInterface()
{
    delete d;
}

MusicAlbum DatabaseInterface::albumFromIndex(int albumIndex) const
{

    if (albumIndex < 0 || albumIndex >= d->mIndexByPosition.length()) {
        return {};
    }

    auto result = albumFromId(d->mIndexByPosition[albumIndex]);

    return result;
}

MusicAlbum DatabaseInterface::albumFromId(qlonglong albumId) const
{
    auto selectAlbumQueryText = QStringLiteral("SELECT `ID`, "
                                               "`Title`, "
                                               "`AlbumInternalID`, "
                                               "`Artist`, "
                                               "`CoverFileName`, "
                                               "`TracksCount` "
                                               "FROM `Albums`"
                                               "WHERE "
                                               "`ID` = :albumId");

    QSqlQuery selectAlbumQuery(d->mTracksDatabase);
    auto result = selectAlbumQuery.prepare(selectAlbumQueryText);

    if (!result) {
        qDebug() << "DatabaseInterface::albumFromId" << selectAlbumQuery.lastError();

        return {};
    }

    selectAlbumQuery.bindValue(QStringLiteral(":albumId"), albumId);

    result = selectAlbumQuery.exec();

    if (!result || !selectAlbumQuery.isSelect() || !selectAlbumQuery.isActive()) {
        qDebug() << "DatabaseInterface::albumFromId" << "not select" << selectAlbumQuery.lastQuery();
        qDebug() << "DatabaseInterface::albumFromId" << selectAlbumQuery.lastError();

        return {};
    }

    if (!selectAlbumQuery.next()) {
        return {};
    }

    auto itAlbum = d->mAlbumCache.find(albumId);

    if (itAlbum == d->mAlbumCache.end()) {
        MusicAlbum retrievedAlbum;

        retrievedAlbum.setDatabaseId(selectAlbumQuery.record().value(0).toLongLong());
        retrievedAlbum.setTitle(selectAlbumQuery.record().value(1).toString());
        retrievedAlbum.setId(selectAlbumQuery.record().value(2).toString());
        retrievedAlbum.setArtist(selectAlbumQuery.record().value(3).toString());
        retrievedAlbum.setAlbumArtURI(selectAlbumQuery.record().value(4).toUrl());
        retrievedAlbum.setTracksCount(selectAlbumQuery.record().value(5).toInt());
        retrievedAlbum.setTracks(fetchTracks(retrievedAlbum.databaseId()));
        retrievedAlbum.setTrackIds(retrievedAlbum.tracksKeys());
        retrievedAlbum.setValid(true);

        d->mAlbumCache[albumId] = retrievedAlbum;

        return retrievedAlbum;
    }

    return itAlbum.value();
}

int DatabaseInterface::albumIdFromTrackId(quintptr trackId) const
{
    auto selectTrackQueryText = QStringLiteral("SELECT `AlbumID` FROM `Tracks` "
                                          "WHERE "
                                          "`ID` = :trackId");

    QSqlQuery selectTrackQuery(d->mTracksDatabase);
    auto result = selectTrackQuery.prepare(selectTrackQueryText);

    if (!result) {
        qDebug() << "DatabaseInterface::albumIdFromTrackId" << selectTrackQuery.lastError();
    }

    selectTrackQuery.bindValue(QStringLiteral(":trackId"), trackId);

    result = selectTrackQuery.exec();

    if (!result || !selectTrackQuery.isSelect() || !selectTrackQuery.isActive()) {
        qDebug() << "DatabaseInterface::insertAlbumsList" << "not select" << selectTrackQuery.lastQuery();
        qDebug() << "DatabaseInterface::insertAlbumsList" << selectTrackQuery.lastError();

        return -1;
    }

    if (!selectTrackQuery.next()) {
        return -1;
    }

    return selectTrackQuery.record().value(0).toInt();
}

int DatabaseInterface::albumCount() const
{
    auto selectAlbumQueryText = QStringLiteral("SELECT count(*) "
                                               "FROM Albums");

    QSqlQuery selectAlbumQuery(d->mTracksDatabase);
    const auto result = selectAlbumQuery.prepare(selectAlbumQueryText);

    if (!result) {
        qDebug() << "DatabaseInterface::albumCount" << selectAlbumQuery.lastError();

        return 0;
    }

    selectAlbumQuery.exec();

    if (!selectAlbumQuery.isSelect() || !selectAlbumQuery.isActive()) {
        qDebug() << "DatabaseInterface::albumsList" << "not select" << selectAlbumQuery.lastQuery();
        qDebug() << "DatabaseInterface::albumsList" << selectAlbumQuery.lastError();

        return 0;
    }

    if (!selectAlbumQuery.next()) {
        return 0;
    }

    return selectAlbumQuery.record().value(0).toInt();
}

int DatabaseInterface::albumPositionByIndex(qlonglong index) const
{
    return d->mPositionByIndex[index];
}

void DatabaseInterface::insertAlbumsList(const QVector<MusicAlbum> &allAlbums)
{
    auto selectAlbumQueryText = QStringLiteral("SELECT ID FROM `Albums` "
                                          "WHERE "
                                          "`Title` = :title");/* AND "
                                          "`Artist` = :artist");*/

    QSqlQuery selectAlbumQuery(d->mTracksDatabase);
    auto result = selectAlbumQuery.prepare(selectAlbumQueryText);

    if (!result) {
        qDebug() << "DatabaseInterface::insertAlbumsList" << selectAlbumQuery.lastError();
    }

    auto insertAlbumQueryText = QStringLiteral("INSERT INTO Albums (`Title`, `Artist`, `CoverFileName`, `TracksCount`)"
                                          "VALUES (:title, :artist, :coverFileName, :tracksCount)");

    QSqlQuery insertAlbumQuery(d->mTracksDatabase);
    result = insertAlbumQuery.prepare(insertAlbumQueryText);

    if (!result) {
        qDebug() << "DatabaseInterface::insertAlbumsList" << insertAlbumQuery.lastError();
    }

    int albumId = 0;
    for(const auto &album : allAlbums) {
        auto transactionResult = d->mTracksDatabase.transaction();
        if (!transactionResult) {
            qDebug() << "transaction failed";
            return;
        }

        selectAlbumQuery.bindValue(QStringLiteral(":title"), album.title());
        selectAlbumQuery.bindValue(QStringLiteral(":artist"), album.artist());

        result = selectAlbumQuery.exec();

        if (!result || !selectAlbumQuery.isSelect() || !selectAlbumQuery.isActive()) {
            qDebug() << "DatabaseInterface::insertAlbumsList" << "not select" << selectAlbumQuery.lastQuery();
            qDebug() << "DatabaseInterface::insertAlbumsList" << selectAlbumQuery.lastError();
        }

        if (selectAlbumQuery.next()) {
            albumId = std::max(albumId, selectAlbumQuery.record().value(0).toInt());
        } else {
            insertAlbumQuery.bindValue(QStringLiteral(":title"), album.title());
            insertAlbumQuery.bindValue(QStringLiteral(":artist"), album.artist());
            insertAlbumQuery.bindValue(QStringLiteral(":coverFileName"), album.albumArtURI());
            insertAlbumQuery.bindValue(QStringLiteral(":tracksCount"), album.tracksCount());

            qDebug() << "new album" << album.title();

            result = insertAlbumQuery.exec();

            if (!result || !insertAlbumQuery.isActive()) {
                qDebug() << "DatabaseInterface::insertAlbumsList" << "not select" << insertAlbumQuery.lastQuery();
                qDebug() << "DatabaseInterface::insertAlbumsList" << insertAlbumQuery.lastError();

                transactionResult = d->mTracksDatabase.commit();
                if (!transactionResult) {
                    qDebug() << "commit failed";
                    return;
                }

                continue;
            }

            result = selectAlbumQuery.exec();

            if (!result || !selectAlbumQuery.isSelect() || !selectAlbumQuery.isActive()) {
                qDebug() << "DatabaseInterface::insertAlbumsList" << "not select" << selectAlbumQuery.lastQuery();
                qDebug() << "DatabaseInterface::insertAlbumsList" << selectAlbumQuery.lastError();

                transactionResult = d->mTracksDatabase.commit();
                if (!transactionResult) {
                    qDebug() << "commit failed";
                    return;
                }

                continue;
            }

            if (selectAlbumQuery.next()) {
                albumId = std::max(albumId, selectAlbumQuery.record().value(0).toInt());
            }
        }

        transactionResult = d->mTracksDatabase.commit();
        if (!transactionResult) {
            qDebug() << "commit failed";
            return;
        }
    }

    qDebug() << "maximum albumId" << albumId;

    updateIndexCache();
}

void DatabaseInterface::insertTracksList(QHash<QString, QVector<MusicAudioTrack> > tracks, QHash<QString, QString> covers)
{
    auto selectTrackQueryText = QStringLiteral("SELECT ID FROM `Tracks` "
                                          "WHERE "
                                          "`Title` = :title AND "
                                          "`AlbumID` = :album AND "
                                          "`Artist` = :artist");

    QSqlQuery selectTrackQuery(d->mTracksDatabase);
    auto result = selectTrackQuery.prepare(selectTrackQueryText);

    if (!result) {
        qDebug() << "DatabaseInterface::insertTracksList" << selectTrackQuery.lastError();
    }

    auto insertTrackQueryText = QStringLiteral("INSERT INTO `Tracks` (`Title`, `AlbumID`, `Artist`, `FileName`, `TrackNumber`, `Duration`)"
                                          "VALUES (:title, :album, :artist, :fileName, :trackNumber, :trackDuration)");

    QSqlQuery insertTrackQuery(d->mTracksDatabase);
    result = insertTrackQuery.prepare(insertTrackQueryText);

    if (!result) {
        qDebug() << "DatabaseInterface::insertTracksList" << insertTrackQuery.lastError();
    }

    auto selectAlbumQueryText = QStringLiteral("SELECT ID FROM `Albums` "
                                          "WHERE "
                                          "`Title` = :title");/* AND "
                                          "`Artist` = :artist");*/

    QSqlQuery selectAlbumQuery(d->mTracksDatabase);
    result = selectAlbumQuery.prepare(selectAlbumQueryText);

    if (!result) {
        qDebug() << "DatabaseInterface::insertTracksList" << selectAlbumQuery.lastError();
    }

    auto insertAlbumQueryText = QStringLiteral("INSERT INTO `Albums` (`Title`, `Artist`, `CoverFileName`, `TracksCount`)"
                                          "VALUES (:title, :artist, :coverFileName, :tracksCount)");

    QSqlQuery insertAlbumQuery(d->mTracksDatabase);
    result = insertAlbumQuery.prepare(insertAlbumQueryText);

    if (!result) {
        qDebug() << "DatabaseInterface::insertTracksList" << insertAlbumQuery.lastError();
    }

    int maximumAlbumId = 0;
    quintptr albumId = 0;
    for (const auto &album : tracks) {
        MusicAlbum newAlbum;

        auto transactionResult = d->mTracksDatabase.transaction();
        if (!transactionResult) {
            qDebug() << "transaction failed";
            return;
        }

        for(const auto &track : album) {
            if (newAlbum.artist().isNull()) {
                newAlbum.setArtist(track.artist());
            }

            if (newAlbum.title().isNull()) {
                newAlbum.setTitle(track.albumName());
            }

            if (newAlbum.albumArtURI().isEmpty()) {
                newAlbum.setAlbumArtURI(QUrl::fromLocalFile(covers[track.albumName()]));
            }

            if (!newAlbum.artist().isNull() && !newAlbum.title().isNull() && !newAlbum.albumArtURI().isEmpty()) {
                break;
            }
        }

        newAlbum.setTracksCount(album.size());

        selectAlbumQuery.bindValue(QStringLiteral(":title"), newAlbum.title());
        selectAlbumQuery.bindValue(QStringLiteral(":artist"), newAlbum.artist());

        result = selectAlbumQuery.exec();

        if (!result || !selectAlbumQuery.isSelect() || !selectAlbumQuery.isActive()) {
            qDebug() << "DatabaseInterface::insertTracksList" << "not select" << selectAlbumQuery.lastQuery();
            qDebug() << "DatabaseInterface::insertTracksList" << selectAlbumQuery.lastError();
        }

        if (selectAlbumQuery.next()) {
            albumId = selectAlbumQuery.record().value(0).toInt();
            maximumAlbumId = std::max(maximumAlbumId, selectAlbumQuery.record().value(0).toInt());
        } else {
            insertAlbumQuery.bindValue(QStringLiteral(":title"), newAlbum.title());
            insertAlbumQuery.bindValue(QStringLiteral(":artist"), newAlbum.artist());
            insertAlbumQuery.bindValue(QStringLiteral(":coverFileName"), newAlbum.albumArtURI());
            insertAlbumQuery.bindValue(QStringLiteral(":tracksCount"), newAlbum.tracksCount());

            qDebug() << "new album" << newAlbum.title() << newAlbum.artist();

            result = insertAlbumQuery.exec();

            if (!result) {
                qDebug() << "DatabaseInterface::insertTracksList" << insertAlbumQuery.lastQuery();
                qDebug() << "DatabaseInterface::insertTracksList" << insertAlbumQuery.lastError();

                transactionResult = d->mTracksDatabase.commit();
                if (!transactionResult) {
                    qDebug() << "commit failed";
                    return;
                }

                continue;
            }

            result = selectAlbumQuery.exec();

            if (!result || !selectAlbumQuery.isSelect() || !selectAlbumQuery.isActive()) {
                qDebug() << "DatabaseInterface::insertTracksList" << "not select" << selectAlbumQuery.lastQuery();
                qDebug() << "DatabaseInterface::insertTracksList" << selectAlbumQuery.lastError();

                transactionResult = d->mTracksDatabase.commit();
                if (!transactionResult) {
                    qDebug() << "commit failed";
                    return;
                }

                continue;
            }

            if (selectAlbumQuery.next()) {
                albumId = selectAlbumQuery.record().value(0).toInt();
                maximumAlbumId = std::max(maximumAlbumId, selectAlbumQuery.record().value(0).toInt());
            }
        }

        for(const auto &track : album) {
            //quintptr currentElementId = 0;
            QString artistName = track.artist();

            if (artistName.isEmpty()) {
                artistName = newAlbum.artist();
            }

            selectTrackQuery.bindValue(QStringLiteral(":title"), track.title());
            selectTrackQuery.bindValue(QStringLiteral(":album"), albumId);
            selectTrackQuery.bindValue(QStringLiteral(":artist"), artistName);

            result = selectTrackQuery.exec();

            if (!result || !selectTrackQuery.isSelect() || !selectTrackQuery.isActive()) {
                qDebug() << "DatabaseInterface::insertTracksList" << "not select" << selectTrackQuery.lastQuery();
                qDebug() << "DatabaseInterface::insertTracksList" << selectTrackQuery.lastError();
            }

            if (selectTrackQuery.next()) {
                //currentElementId = selectTrackQuery.record().value(0).toInt();
                continue;
            } else {
                insertTrackQuery.bindValue(QStringLiteral(":title"), track.title());
                insertTrackQuery.bindValue(QStringLiteral(":album"), albumId);
                insertTrackQuery.bindValue(QStringLiteral(":artist"), artistName);
                insertTrackQuery.bindValue(QStringLiteral(":fileName"), track.resourceURI());
                insertTrackQuery.bindValue(QStringLiteral(":trackNumber"), track.trackNumber());
                insertTrackQuery.bindValue(QStringLiteral(":trackDuration"), QVariant::fromValue<qlonglong>(track.duration().msecsSinceStartOfDay()));

                qDebug() << track.title() << artistName << QVariant::fromValue<qlonglong>(track.duration().msecsSinceStartOfDay());

                result = insertTrackQuery.exec();

                if (!result || !insertTrackQuery.isActive()) {
                    qDebug() << "DatabaseInterface::insertTracksList" << insertTrackQuery.lastQuery();
                    qDebug() << "DatabaseInterface::insertTracksList" << insertTrackQuery.lastError();
                }

                result = selectTrackQuery.exec();

                if (!result || !selectTrackQuery.isSelect() || !selectTrackQuery.isActive()) {
                    qDebug() << "DatabaseInterface::insertTracksList" << "not select" << selectTrackQuery.lastQuery();
                    qDebug() << "DatabaseInterface::insertTracksList" << selectTrackQuery.lastError();
                }

                if (selectTrackQuery.next()) {
                    //auto currentElementId = selectTrackQuery.record().value(0).toInt();
                    //qDebug() << "DatabaseInterface::insertTracksList" << "insert track" << track.mTitle << artistName << track.mAlbumName << currentElementId << albumId;
                }
            }
        }

        transactionResult = d->mTracksDatabase.commit();
        if (!transactionResult) {
            qDebug() << "commit failed";
            return;
        }
    }

    qDebug() << "maximum albumId" << albumId;

    updateIndexCache();
}

void DatabaseInterface::initDatabase() const
{
    if (!d->mTracksDatabase.tables().contains(QStringLiteral("DiscoverSource"))) {
        QSqlQuery createSchemaQuery(d->mTracksDatabase);

        const auto &result = createSchemaQuery.exec(QStringLiteral("CREATE TABLE `DiscoverSource` (`ID` INTEGER PRIMARY KEY NOT NULL, "
                                              "`UUID` TEXT NOT NULL, "
                                              "UNIQUE (`UUID`))"));

        if (!result) {
            qDebug() << "AbstractAlbumModel::initDatabase" << createSchemaQuery.lastError();
        }
    }

    if (!d->mTracksDatabase.tables().contains(QStringLiteral("Albums"))) {
        QSqlQuery createSchemaQuery(d->mTracksDatabase);

        const auto &result = createSchemaQuery.exec(QStringLiteral("CREATE TABLE `Albums` (`ID` INTEGER PRIMARY KEY NOT NULL, "
                                                                   "`Title` TEXT NOT NULL, "
                                                                   "`Artist` TEXT NOT NULL, "
                                                                   "`CoverFileName` TEXT NOT NULL, "
                                                                   "`TracksCount` INTEGER NOT NULL, "
                                                                   "`AlbumInternalID` TEXT, "
                                                                   "UNIQUE (`Title`, `Artist`))"));

        if (!result) {
            qDebug() << "AbstractAlbumModel::initDatabase" << createSchemaQuery.lastError();
        }
    }

    if (!d->mTracksDatabase.tables().contains(QStringLiteral("Tracks"))) {
        QSqlQuery createSchemaQuery(d->mTracksDatabase);

        const auto &result = createSchemaQuery.exec(QStringLiteral("CREATE TABLE `Tracks` (`ID` INTEGER PRIMARY KEY NOT NULL, "
                                                                   "`Title` TEXT NOT NULL, "
                                                                   "`AlbumID` INTEGER NOT NULL, "
                                                                   "`Artist` TEXT NOT NULL, "
                                                                   "`FileName` TEXT NOT NULL UNIQUE, "
                                                                   "`TrackNumber` INTEGER NOT NULL, "
                                                                   "`Duration` INTEGER NOT NULL, "
                                                                   "UNIQUE (`Title`, `AlbumID`, `Artist`), "
                                                                   "CONSTRAINT fk_album FOREIGN KEY (`AlbumID`) REFERENCES `Albums`(`ID`))"));

        if (!result) {
            qDebug() << "AbstractAlbumModel::initDatabase" << createSchemaQuery.lastError();
        }
    }

    if (!d->mTracksDatabase.tables().contains(QStringLiteral("TracksMapping"))) {
        QSqlQuery createSchemaQuery(d->mTracksDatabase);

        const auto &result = createSchemaQuery.exec(QStringLiteral("CREATE TABLE `TracksMapping` ("
                                                                   "`TrackID` INTEGER NOT NULL, "
                                                                   "`DiscoverID` INTEGER NOT NULL, "
                                                                   "PRIMARY KEY (`TrackID`, `DiscoverID`), "
                                                                   "CONSTRAINT fk_trackID FOREIGN KEY (`TrackID`) REFERENCES `Tracks`(`ID`), "
                                                                   "CONSTRAINT fk_discoverID FOREIGN KEY (`DiscoverID`) REFERENCES `DiscoverSource`(`ID`))"));

        if (!result) {
            qDebug() << "AbstractAlbumModel::initDatabase" << createSchemaQuery.lastError();
        }
    }

    QSqlQuery createTrackIndex(d->mTracksDatabase);

    const auto &result = createTrackIndex.exec(QStringLiteral("CREATE INDEX "
                                                              "IF NOT EXISTS "
                                                              "`TracksAlbumIndex` ON `Tracks` "
                                                              "(`AlbumID`)"));

    if (!result) {
        qDebug() << "AbstractAlbumModel::initDatabase" << createTrackIndex.lastError();
    }
}

QMap<qlonglong, MusicAudioTrack> DatabaseInterface::fetchTracks(qlonglong albumId) const
{
    QMap<qlonglong, MusicAudioTrack> allTracks;

    auto selectTrackQueryText = QStringLiteral("SELECT `ID`, "
                                               "`Title`, "
                                               "`AlbumID`, "
                                               "`Artist`, "
                                               "`FileName`, "
                                               "`TrackNumber`, "
                                               "`Duration` "
                                               "FROM `Tracks` "
                                               "WHERE "
                                               "`AlbumID` = :albumId");



    QSqlQuery selectTrackQuery(d->mTracksDatabase);
    auto result = selectTrackQuery.prepare(selectTrackQueryText);

    if (!result) {
        qDebug() << "DatabaseInterface::fetchTracks" << selectTrackQuery.lastError();

        return {};
    }

    selectTrackQuery.bindValue(QStringLiteral(":albumId"), albumId);

    result = selectTrackQuery.exec();

    if (!result || !selectTrackQuery.isSelect() || !selectTrackQuery.isActive()) {
        qDebug() << "DatabaseInterface::fetchTracks" << "not select" << selectTrackQuery.lastQuery();
        qDebug() << "DatabaseInterface::fetchTracks" << selectTrackQuery.lastError();
    }

    while (selectTrackQuery.next()) {
        MusicAudioTrack newTrack;

        newTrack.setDatabaseId(selectTrackQuery.record().value(0).toLongLong());
        newTrack.setTitle(selectTrackQuery.record().value(1).toString());
        newTrack.setParentId(selectTrackQuery.record().value(2).toString());
        newTrack.setArtist(selectTrackQuery.record().value(3).toString());
        newTrack.setResourceURI(selectTrackQuery.record().value(4).toUrl());
        newTrack.setTrackNumber(selectTrackQuery.record().value(5).toInt());

        newTrack.setDuration(QTime::fromMSecsSinceStartOfDay(selectTrackQuery.record().value(6).toInt()));
        qDebug() << newTrack.title() << newTrack.artist() << newTrack.duration() << selectTrackQuery.record().value(6) << selectTrackQuery.record().value(6).toInt();

        newTrack.setValid(true);

        allTracks[newTrack.databaseId()] = newTrack;
    }

    updateTracksCount(albumId, allTracks.size());

    return allTracks;
}

void DatabaseInterface::updateTracksCount(qlonglong albumId, int tracksCount) const
{

    auto selectAlbumQueryText = QStringLiteral("SELECT `TracksCount` "
                                               "FROM `Albums`"
                                               "WHERE "
                                               "`ID` = :albumId");

    QSqlQuery selectAlbumQuery(d->mTracksDatabase);
    auto result = selectAlbumQuery.prepare(selectAlbumQueryText);

    if (!result) {
        qDebug() << "DatabaseInterface::albumFromId" << selectAlbumQuery.lastError();

        return;
    }

    selectAlbumQuery.bindValue(QStringLiteral(":albumId"), albumId);

    result = selectAlbumQuery.exec();

    if (!result || !selectAlbumQuery.isSelect() || !selectAlbumQuery.isActive()) {
        qDebug() << "DatabaseInterface::albumFromId" << "not select" << selectAlbumQuery.lastQuery();
        qDebug() << "DatabaseInterface::albumFromId" << selectAlbumQuery.lastError();

        return;
    }

    if (!selectAlbumQuery.next()) {
        return;
    }

    auto oldTracksCount = selectAlbumQuery.record().value(0).toInt();

    if (oldTracksCount != tracksCount) {
        auto updateAlbumQueryText = QStringLiteral("UPDATE `Albums` "
                                                   "SET `TracksCount`=:tracksCount "
                                                   "WHERE "
                                                   "`ID` = :albumId");

        QSqlQuery updateAlbumQuery(d->mTracksDatabase);
        auto result = updateAlbumQuery.prepare(updateAlbumQueryText);

        if (!result) {
            qDebug() << "DatabaseInterface::albumFromId" << updateAlbumQuery.lastError();

            return;
        }

        updateAlbumQuery.bindValue(QStringLiteral(":tracksCount"), tracksCount);
        updateAlbumQuery.bindValue(QStringLiteral(":albumId"), albumId);

        result = updateAlbumQuery.exec();

        if (!result || !updateAlbumQuery.isActive()) {
            qDebug() << "DatabaseInterface::albumFromId" << "not select" << updateAlbumQuery.lastQuery();
            qDebug() << "DatabaseInterface::albumFromId" << updateAlbumQuery.lastError();

            return;
        }
    }
}

void DatabaseInterface::updateIndexCache()
{
    initDatabase();

    MusicAlbum retrievedAlbum;

    auto selectAlbumQueryText = QStringLiteral("SELECT `ID` "
                                               "FROM `Albums`");

    QSqlQuery selectAlbumQuery(d->mTracksDatabase);
    auto result = selectAlbumQuery.prepare(selectAlbumQueryText);

    if (!result) {
        qDebug() << "DatabaseInterface::albumFromIndex" << selectAlbumQuery.lastError();

        return;
    }

    result = selectAlbumQuery.exec();

    if (!result || !selectAlbumQuery.isSelect() || !selectAlbumQuery.isActive()) {
        qDebug() << "DatabaseInterface::albumFromIndex" << "not select" << selectAlbumQuery.lastQuery();
        qDebug() << "DatabaseInterface::albumFromIndex" << selectAlbumQuery.lastError();

        return;
    }

    d->mIndexByPosition.clear();
    d->mPositionByIndex.clear();
    d->mAlbumCache.clear();

    while(selectAlbumQuery.next()) {
        d->mPositionByIndex[selectAlbumQuery.record().value(0).toLongLong()] = d->mIndexByPosition.length();
        d->mIndexByPosition.push_back(selectAlbumQuery.record().value(0).toLongLong());
    }

    Q_EMIT resetModel();
}


#include "moc_databaseinterface.cpp"
