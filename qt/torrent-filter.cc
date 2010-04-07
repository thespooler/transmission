/*
 * This file Copyright (C) 2009-2010 Mnemosyne LLC
 *
 * This file is licensed by the GPL version 2.  Works owned by the
 * Transmission project are granted a special exemption to clause 2(b)
 * so that the bulk of its code can remain under the MIT license.
 * This exemption does not extend to derived works not owned by
 * the Transmission project.
 *
 * $Id$
 */

#include <iostream>

#include "filters.h"
#include "prefs.h"
#include "torrent.h"
#include "torrent-filter.h"
#include "torrent-model.h"

TorrentFilter :: TorrentFilter( Prefs& prefs ):
    myPrefs( prefs ),
    myTextMode( FILTER_BY_NAME )
{
    // listen for changes to the preferences to know when to refilter / resort
    connect( &myPrefs, SIGNAL(changed(int)), this, SLOT(refreshPref(int)));

    setDynamicSortFilter( true );

    // initialize our state from the current prefs
    QList<int> initKeys;
    initKeys << Prefs :: SORT_MODE
             << Prefs :: FILTER_MODE;
    foreach( int key, initKeys )
        refreshPref( key );
}

TorrentFilter :: ~TorrentFilter( )
{
}

void
TorrentFilter :: refreshPref( int key )
{
    switch( key )
    {
        case Prefs :: SORT_MODE:
        case Prefs :: SORT_REVERSED:
            sort( 0, myPrefs.getBool(Prefs::SORT_REVERSED) ? Qt::AscendingOrder : Qt::DescendingOrder );
            invalidate( );
            break;
        case Prefs :: FILTER_MODE:
            invalidateFilter( );
            break;
    }
}

/***
****
***/

void
TorrentFilter :: setTextMode( int i )
{
    if( myTextMode != i )
    {
        myTextMode = TextMode( i );
        invalidateFilter( );
    }
}

void
TorrentFilter :: setText( QString text )
{
    QString trimmed = text.trimmed( );

    if( myText != trimmed )
    {
        myText = trimmed;
        invalidateFilter( );
    }
}

/***
****
***/

namespace
{
    template <typename T> int compare( const T a, const T b )
    {
        if( a < b ) return -1;
        if( b < a ) return 1;
        return 0;
    }
}

bool
TorrentFilter :: lessThan( const QModelIndex& left, const QModelIndex& right ) const
{
    const Torrent * a = sourceModel()->data( left, TorrentModel::TorrentRole ).value<const Torrent*>();
    const Torrent * b = sourceModel()->data( right, TorrentModel::TorrentRole ).value<const Torrent*>();
    int less = 0;

    switch( myPrefs.get<SortMode>(Prefs::SORT_MODE).mode() )
    {
        case SortMode :: SORT_BY_SIZE:
            less = compare( a->sizeWhenDone(), b->sizeWhenDone() );
            break;
        case SortMode :: SORT_BY_ACTIVITY:
            less = compare( a->downloadSpeed() + a->uploadSpeed(), b->downloadSpeed() + b->uploadSpeed() );
            if( !less )
                less = compare( a->uploadedEver(), b->uploadedEver() );
            break;
        case SortMode :: SORT_BY_AGE:
            less = compare( a->dateAdded().toTime_t(), b->dateAdded().toTime_t() );
            break;
        case SortMode :: SORT_BY_ID:
            less = compare( a->id(), b->id() );
            break;
        case SortMode :: SORT_BY_STATE:
            if( a->hasError() != b->hasError() )
                less = a->hasError();
            else
                less = compare( a->getActivity(), b->getActivity() );
            if( less )
                break;
        case SortMode :: SORT_BY_PROGRESS:
            less = compare( a->percentDone(), b->percentDone() );
            if( less )
                break;
        case SortMode :: SORT_BY_RATIO:
            less = a->compareRatio( *b );
            break;
        case SortMode :: SORT_BY_ETA:
            less = a->compareETA( *b );
            break;
        case SortMode :: SORT_BY_TRACKER:
            less = a->compareTracker( *b );
            break;
        default:
            break;
    }
    if( less == 0 )
        less = -a->name().compare( b->name(), Qt::CaseInsensitive );
    if( less == 0 )
        less = compare( a->hashString(), b->hashString() );
    return less < 0;
}


/***
****
***/

bool
TorrentFilter :: filterAcceptsRow( int sourceRow, const QModelIndex& sourceParent ) const
{
    QModelIndex childIndex = sourceModel()->index( sourceRow, 0, sourceParent );
    const Torrent * tor = childIndex.model()->data( childIndex, TorrentModel::TorrentRole ).value<const Torrent*>();
    const tr_torrent_activity activity = tor->getActivity( );
    bool accepts;

    switch( myPrefs.get<FilterMode>(Prefs::FILTER_MODE).mode() )
    {
        case FilterMode::SHOW_ALL:
            accepts = true;
            break;
        case FilterMode::SHOW_ACTIVE:
            accepts = tor->peersWeAreUploadingTo( ) > 0 || tor->peersWeAreDownloadingFrom( ) > 0 || tor->isVerifying( );
            break;
        case FilterMode::SHOW_DOWNLOADING:
            accepts = activity == TR_STATUS_DOWNLOAD;
            break;
        case FilterMode::SHOW_SEEDING:
            accepts = activity == TR_STATUS_SEED;
            break;
        case FilterMode::SHOW_PAUSED:
            accepts = activity == TR_STATUS_STOPPED;
            break;
    }

    if( accepts && !myText.isEmpty( ) ) switch( myTextMode )
    {
        case FILTER_BY_NAME:
            accepts = tor->name().contains( myText, Qt::CaseInsensitive );
            break;
        case FILTER_BY_FILES:
            accepts = tor->hasFileSubstring( myText );
            break;
        case FILTER_BY_TRACKER:
            accepts = tor->hasTrackerSubstring( myText );
            break;
    }

    return accepts;
}

int
TorrentFilter :: hiddenRowCount( ) const
{
    return sourceModel()->rowCount( ) - rowCount( );
}
