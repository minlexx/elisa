/*
 * Copyright 2015-2016 Matthieu Gallien <matthieu_gallien@yahoo.fr>
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

#include "localalbummodel.h"
#include "musicstatistics.h"
#include "localbaloofilelisting.h"
#include "localbalootrack.h"
#include "localbalooalbum.h"

#include <Baloo/File>

#include <QtCore/QUrl>
#include <QtCore/QThread>
#include <QtCore/QTimer>
#include <QtCore/QPointer>
#include <QtCore/QVector>

#include <QtSql/QSqlDatabase>
#include <QtSql/QSqlQuery>
#include <QtSql/QSqlRecord>
#include <QtSql/QSqlError>

class LocalAlbumModelPrivate
{
public:

    bool mUseLocalIcons = false;

    MusicStatistics *mMusicDatabase = nullptr;

    QThread mBalooQueryThread;

    LocalBalooFileListing mFileListing;

    QVector<LocalBalooAlbum> mAlbumsData;

    QMap<quintptr, quintptr> mTracksInAlbums;

    QSqlDatabase mTracksDatabase;

};

LocalAlbumModel::LocalAlbumModel(QObject *parent) : QAbstractItemModel(parent), d(new LocalAlbumModelPrivate)
{
    d->mBalooQueryThread.start();
    d->mFileListing.moveToThread(&d->mBalooQueryThread);

    connect(this, &LocalAlbumModel::refreshContent, &d->mFileListing, &LocalBalooFileListing::refreshContent, Qt::QueuedConnection);
    connect(&d->mFileListing, &LocalBalooFileListing::tracksList, this, &LocalAlbumModel::tracksList);

    d->mTracksDatabase = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"));
    d->mTracksDatabase.setDatabaseName(QStringLiteral(":memory:"));
    d->mTracksDatabase.setConnectOptions(QStringLiteral("foreign_keys = ON"));
    auto result = d->mTracksDatabase.open();
    if (result) {
        qDebug() << "database open";
    } else {
        qDebug() << "database not open";
    }

    QMetaObject::invokeMethod(&d->mFileListing, "init", Qt::QueuedConnection);

    Q_EMIT refreshContent();
}

LocalAlbumModel::~LocalAlbumModel()
{
    delete d;
}

int LocalAlbumModel::rowCount(const QModelIndex &parent) const
{
    if (!parent.isValid()) {
        return d->mAlbumsData.size();
    }

    if (parent.row() < 0 || parent.row() >= d->mAlbumsData.size()) {
        return 0;
    }

    return d->mAlbumsData[parent.row()].mTrackIds.size();
}

QHash<int, QByteArray> LocalAlbumModel::roleNames() const
{
    QHash<int, QByteArray> roles;

    roles[static_cast<int>(ColumnsRoles::TitleRole)] = "title";
    roles[static_cast<int>(ColumnsRoles::DurationRole)] = "duration";
    roles[static_cast<int>(ColumnsRoles::ArtistRole)] = "artist";
    roles[static_cast<int>(ColumnsRoles::AlbumRole)] = "album";
    roles[static_cast<int>(ColumnsRoles::TrackNumberRole)] = "trackNumber";
    roles[static_cast<int>(ColumnsRoles::RatingRole)] = "rating";
    roles[static_cast<int>(ColumnsRoles::ImageRole)] = "image";
    roles[static_cast<int>(ColumnsRoles::ItemClassRole)] = "itemClass";
    roles[static_cast<int>(ColumnsRoles::CountRole)] = "count";
    roles[static_cast<int>(ColumnsRoles::IsPlayingRole)] = "isPlaying";

    return roles;
}

Qt::ItemFlags LocalAlbumModel::flags(const QModelIndex &index) const
{
    if (!index.isValid()) {
        return Qt::NoItemFlags;
    }

    return Qt::ItemIsSelectable | Qt::ItemIsEnabled;
}

QVariant LocalAlbumModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid()) {
        return {};
    }

    if (index.column() != 0) {
        return {};
    }

    if (index.row() < 0) {
        return {};
    }

    if (!index.parent().isValid()) {
        if (index.internalId() == 0) {
            if (index.row() < 0 || index.row() >= d->mAlbumsData.size()) {
                return {};
            }

            const auto &itAlbum = d->mAlbumsData[index.row()];
            return internalDataAlbum(itAlbum, role);
        }
    }

    auto itTrack = d->mTracksInAlbums.find(index.internalId());
    if (itTrack == d->mTracksInAlbums.end()) {
        return {};
    }

    auto albumId = itTrack.value();
    if (albumId >= quintptr(d->mAlbumsData.size())) {
        return {};
    }

    const auto &currentAlbum = d->mAlbumsData[albumId];

    if (index.row() < 0 || index.row() >= currentAlbum.mNbTracks) {
        return {};
    }

    auto itTrackInCurrentAlbum = currentAlbum.mTracks.find(index.internalId());
    if (itTrackInCurrentAlbum == currentAlbum.mTracks.end()) {
        return {};
    }

    return internalDataTrack(itTrackInCurrentAlbum.value(), index, role);
}

QVariant LocalAlbumModel::internalDataAlbum(const LocalBalooAlbum &albumData, int role) const
{
    ColumnsRoles convertedRole = static_cast<ColumnsRoles>(role);

    switch(convertedRole)
    {
    case ColumnsRoles::TitleRole:
        return albumData.mTitle;
    case ColumnsRoles::DurationRole:
        return {};
    case ColumnsRoles::CreatorRole:
        return {};
    case ColumnsRoles::ArtistRole:
        return albumData.mArtist;
    case ColumnsRoles::AlbumRole:
        return {};
    case ColumnsRoles::TrackNumberRole:
        return {};
    case ColumnsRoles::RatingRole:
        return {};
    case ColumnsRoles::ImageRole:
        if (albumData.mCoverFile.isValid()) {
            return albumData.mCoverFile;
        } else {
            if (d->mUseLocalIcons) {
                return QUrl(QStringLiteral("qrc:/media-optical-audio.svg"));
            } else {
                return QUrl(QStringLiteral("image://icon/media-optical-audio"));
            }
        }
    case ColumnsRoles::ResourceRole:
        return {};
    case ColumnsRoles::ItemClassRole:
        return {};
    case ColumnsRoles::CountRole:
        return albumData.mNbTracks;
    case ColumnsRoles::IdRole:
        return albumData.mTitle;
    case ColumnsRoles::IsPlayingRole:
        return {};
    }

    return {};
}

QVariant LocalAlbumModel::internalDataTrack(const LocalBalooTrack &track, const QModelIndex &index, int role) const
{
    ColumnsRoles convertedRole = static_cast<ColumnsRoles>(role);

    switch(convertedRole)
    {
    case ColumnsRoles::TitleRole:
        return track.mTitle;
    case ColumnsRoles::DurationRole:
    {
        QTime trackDuration = QTime::fromMSecsSinceStartOfDay(1000 * track.mDuration);
        if (trackDuration.hour() == 0) {
            return trackDuration.toString(QStringLiteral("mm:ss"));
        } else {
            return trackDuration.toString();
        }
    }
    case ColumnsRoles::CreatorRole:
        return track.mArtist;
    case ColumnsRoles::ArtistRole:
        return track.mArtist;
    case ColumnsRoles::AlbumRole:
        return track.mAlbum;
    case ColumnsRoles::TrackNumberRole:
        return track.mTrackNumber;
    case ColumnsRoles::RatingRole:
        return 0;
    case ColumnsRoles::ImageRole:
        return data(index.parent(), role);
    case ColumnsRoles::ResourceRole:
        return track.mFile;
    case ColumnsRoles::ItemClassRole:
        return {};
    case ColumnsRoles::CountRole:
        return {};
    case ColumnsRoles::IdRole:
        return track.mTitle;
    case ColumnsRoles::IsPlayingRole:
        return false;
    }

    return {};
}

void LocalAlbumModel::initDatabase()
{
    if (!d->mTracksDatabase.tables().contains(QStringLiteral("Albums"))) {
        QSqlQuery createSchemaQuery(d->mTracksDatabase);

        createSchemaQuery.exec(QStringLiteral("CREATE TABLE `Albums` (`ID` INTEGER PRIMARY KEY NOT NULL, "
                                              "`Title` TEXT NOT NULL, "
                                              "`Artist` TEXT NOT NULL, "
                                              "`CoverFileName` TEXT NOT NULL, "
                                              "`TracksCount` INTEGER NOT NULL, "
                                              "UNIQUE (`Title`, `Artist`))"));

        qDebug() << "LocalAlbumModel::initDatabase" << createSchemaQuery.lastError();
    }

    if (!d->mTracksDatabase.tables().contains(QStringLiteral("Tracks"))) {
        QSqlQuery createSchemaQuery(d->mTracksDatabase);

        createSchemaQuery.exec(QStringLiteral("CREATE TABLE `Tracks` (`ID` INTEGER PRIMARY KEY NOT NULL, "
                                              "`Title` TEXT NOT NULL, "
                                              "`Album` TEXT NOT NULL, "
                                              "`Artist` TEXT NOT NULL, "
                                              "`FileName` TEXT NOT NULL UNIQUE, "
                                              "UNIQUE (`Title`, `Album`, `Artist`), "
                                              "CONSTRAINT fk_album FOREIGN KEY (`Album`, `Artist`) REFERENCES `Albums`(`Title`, `Artist`))"));

        qDebug() << "LocalAlbumModel::initDatabase" << createSchemaQuery.lastError();
    }
}

QModelIndex LocalAlbumModel::index(int row, int column, const QModelIndex &parent) const
{
    if (column != 0) {
        return {};
    }

    if (!parent.isValid()) {
        return createIndex(row, column, nullptr);
    }

    if (parent.row() < 0 || parent.row() >= d->mAlbumsData.size()) {
        return {};
    }

    const auto &currentAlbum = d->mAlbumsData[parent.row()];

    if (row >= currentAlbum.mTrackIds.size()) {
        return {};
    }

    return createIndex(row, column, currentAlbum.mTrackIds[row]);
}

QModelIndex LocalAlbumModel::parent(const QModelIndex &child) const
{
    if (!child.isValid()) {
        return {};
    }

    if (child.internalId() == 0) {
        return {};
    }

    auto itTrack = d->mTracksInAlbums.find(child.internalId());
    if (itTrack == d->mTracksInAlbums.end()) {
        return {};
    }

    auto albumId = itTrack.value();

    return index(albumId, 0);
}

int LocalAlbumModel::columnCount(const QModelIndex &parent) const
{
    Q_UNUSED(parent);

    return 1;
}

MusicStatistics *LocalAlbumModel::musicDatabase() const
{
    return d->mMusicDatabase;
}

void LocalAlbumModel::setMusicDatabase(MusicStatistics *musicDatabase)
{
    if (d->mMusicDatabase == musicDatabase)
        return;

    if (d->mMusicDatabase) {
        disconnect(this, &LocalAlbumModel::newAlbum, d->mMusicDatabase, &MusicStatistics::newAlbum);
        disconnect(this, &LocalAlbumModel::newAudioTrack, d->mMusicDatabase, &MusicStatistics::newAudioTrack);
    }

    d->mMusicDatabase = musicDatabase;

    if (d->mMusicDatabase) {
        connect(this, &LocalAlbumModel::newAlbum, d->mMusicDatabase, &MusicStatistics::newAlbum);
        connect(this, &LocalAlbumModel::newAudioTrack, d->mMusicDatabase, &MusicStatistics::newAudioTrack);
    }

    emit musicDatabaseChanged();
}

void LocalAlbumModel::tracksList(const QHash<QString, QList<LocalBalooTrack> > &tracks, const QHash<QString, QString> &covers)
{
    beginResetModel();

    initDatabase();

    d->mAlbumsData.clear();

    auto selectTrackQueryText = QStringLiteral("SELECT ID FROM `Tracks` "
                                          "WHERE "
                                          "`Title` = :title AND "
                                          "`Album` = :album AND "
                                          "`Artist` = :artist");

    QSqlQuery selectTrackQuery(d->mTracksDatabase);
    selectTrackQuery.prepare(selectTrackQueryText);

    auto insertTrackQueryText = QStringLiteral("INSERT INTO Tracks (`Title`, `Album`, `Artist`, `FileName`)"
                                          "VALUES (:title, :album, :artist, :fileName)");

    QSqlQuery insertTrackQuery(d->mTracksDatabase);
    insertTrackQuery.prepare(insertTrackQueryText);

    auto selectAlbumQueryText = QStringLiteral("SELECT ID FROM `Albums` "
                                          "WHERE "
                                          "`Title` = :title AND "
                                          "`Artist` = :artist");

    QSqlQuery selectAlbumQuery(d->mTracksDatabase);
    selectAlbumQuery.prepare(selectAlbumQueryText);

    auto insertAlbumQueryText = QStringLiteral("INSERT INTO Albums (`Title`, `Artist`, `CoverFileName`, `TracksCount`)"
                                          "VALUES (:title, :artist, :coverFileName, :tracksCount)");

    QSqlQuery insertAlbumQuery(d->mTracksDatabase);
    insertAlbumQuery.prepare(insertAlbumQueryText);

    quintptr albumId = 0;
    for (const auto &album : tracks) {
        LocalBalooAlbum newAlbum;

        for(const auto &track : album) {
            if (newAlbum.mArtist.isNull()) {
                newAlbum.mArtist = track.mArtist;
            }

            if (newAlbum.mTitle.isNull()) {
                newAlbum.mTitle = track.mAlbum;
            }

            if (newAlbum.mCoverFile.isEmpty()) {
                newAlbum.mCoverFile = QUrl::fromLocalFile(covers[track.mAlbum]);
            }

            if (!newAlbum.mArtist.isNull() && !newAlbum.mTitle.isNull() && !newAlbum.mCoverFile.isEmpty()) {
                break;
            }
        }

        selectAlbumQuery.bindValue(QStringLiteral(":title"), newAlbum.mTitle);
        selectAlbumQuery.bindValue(QStringLiteral(":artist"), newAlbum.mArtist);

        selectAlbumQuery.exec();

        if (!selectAlbumQuery.isSelect() || !selectAlbumQuery.isActive()) {
            qDebug() << "LocalAlbumModel::tracksList" << "not select" << selectAlbumQuery.lastQuery();
            qDebug() << "LocalAlbumModel::tracksList" << selectAlbumQuery.lastError();
        }

        if (selectAlbumQuery.next()) {
            albumId = selectAlbumQuery.record().value(0).toInt() - 1;
        } else {
            insertAlbumQuery.bindValue(QStringLiteral(":title"), newAlbum.mTitle);
            insertAlbumQuery.bindValue(QStringLiteral(":artist"), newAlbum.mArtist);
            insertAlbumQuery.bindValue(QStringLiteral(":coverFileName"), newAlbum.mCoverFile);
            insertAlbumQuery.bindValue(QStringLiteral(":tracksCount"), 0);

            insertAlbumQuery.exec();

            if (!insertAlbumQuery.isActive()) {
                qDebug() << "LocalAlbumModel::tracksList" << "not select" << insertAlbumQuery.lastQuery();
                qDebug() << "LocalAlbumModel::tracksList" << insertAlbumQuery.lastError();

                continue;
            }

            selectAlbumQuery.exec();

            if (!selectAlbumQuery.isSelect() || !selectAlbumQuery.isActive()) {
                qDebug() << "LocalAlbumModel::tracksList" << "not select" << selectAlbumQuery.lastQuery();
                qDebug() << "LocalAlbumModel::tracksList" << selectAlbumQuery.lastError();

                continue;
            }

            if (selectAlbumQuery.next()) {
                albumId = selectAlbumQuery.record().value(0).toInt() - 1;
            }
        }

        for(const auto &track : album) {
            quintptr currentElementId = 0;
            QString artistName = track.mArtist;

            if (artistName.isEmpty()) {
                artistName = newAlbum.mArtist;
            }

            selectTrackQuery.bindValue(QStringLiteral(":title"), track.mTitle);
            selectTrackQuery.bindValue(QStringLiteral(":album"), track.mAlbum);
            selectTrackQuery.bindValue(QStringLiteral(":artist"), artistName);

            selectTrackQuery.exec();

            if (!selectTrackQuery.isSelect() || !selectTrackQuery.isActive()) {
                qDebug() << "LocalAlbumModel::tracksList" << "not select" << selectTrackQuery.lastQuery();
                qDebug() << "LocalAlbumModel::tracksList" << selectTrackQuery.lastError();
            }

            if (selectTrackQuery.next()) {
                currentElementId = selectTrackQuery.record().value(0).toInt();
                continue;
            } else {
                insertTrackQuery.bindValue(QStringLiteral(":title"), track.mTitle);
                insertTrackQuery.bindValue(QStringLiteral(":album"), track.mAlbum);
                insertTrackQuery.bindValue(QStringLiteral(":artist"), artistName);
                insertTrackQuery.bindValue(QStringLiteral(":fileName"), track.mFile);

                insertTrackQuery.exec();

                if (!insertTrackQuery.isActive()) {
                    qDebug() << "LocalAlbumModel::tracksList" << "not select" << insertTrackQuery.lastQuery();
                    qDebug() << "LocalAlbumModel::tracksList" << insertTrackQuery.lastError();
                }

                selectTrackQuery.exec();

                if (!selectTrackQuery.isSelect() || !selectTrackQuery.isActive()) {
                    qDebug() << "LocalAlbumModel::tracksList" << "not select" << selectTrackQuery.lastQuery();
                    qDebug() << "LocalAlbumModel::tracksList" << selectTrackQuery.lastError();
                }

                if (selectTrackQuery.next()) {
                    currentElementId = selectTrackQuery.record().value(0).toInt();
                }
            }

            newAlbum.mTracks[currentElementId] = track;
            newAlbum.mTracks[currentElementId].mArtist = artistName;
            newAlbum.mTrackIds.push_back(currentElementId);
            d->mTracksInAlbums[currentElementId] = albumId;
        }

        newAlbum.mNbTracks = newAlbum.mTracks.size();

        d->mAlbumsData.push_back(newAlbum);
    }

    endResetModel();
}

#include "moc_localalbummodel.cpp"