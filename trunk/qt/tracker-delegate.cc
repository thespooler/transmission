/*
 * This file Copyright (C) 2009-2010 Mnemosyne LLC
 *
 * This file is licensed by the GPL version 2.  Works owned by the
 * Transmission project are granted a special exemption to clause 2(b)
 * so that the bulk of its code can remain under the MIT license.
 * This exemption does not extend to derived works not owned by
 * the Transmission project.
 *
 * $Id: torrent-delegate.cc 11051 2010-07-24 23:51:02Z charles $
 */

#include <iostream>

#include <QApplication>
#include <QBrush>
#include <QFont>
#include <QFontMetrics>
#include <QIcon>
#include <QModelIndex>
#include <QPainter>
#include <QPixmap>
#include <QPixmapCache>
#include <QStyleOptionProgressBarV2>
#include <QTextDocument>
#include <QUrl>

#include "favicon.h"
#include "formatter.h"
#include "torrent.h"
#include "tracker-delegate.h"
#include "tracker-model.h"

/***
****
***/

namespace
{
    const int mySpacing = 6;
    const QSize myMargin( 10, 6 );
}

QSize
TrackerDelegate :: margin( const QStyle& style ) const
{
    Q_UNUSED( style );

    return myMargin;
}

/***
****
***/

QSize
TrackerDelegate :: sizeHint( const QStyleOptionViewItem& option, const TrackerInfo& info ) const
{
    Q_UNUSED( option );

    QPixmap favicon = info.st.getFavicon( );
    
    const QString text = TrackerDelegate :: getText( info );
    QTextDocument textDoc;
    textDoc.setHtml( text );
    const QSize textSize = textDoc.size().toSize();

    return QSize( myMargin.width() + favicon.width() + mySpacing + textSize.width() + myMargin.width(),
                  myMargin.height() + qMax<int>( favicon.height(), textSize.height() ) + myMargin.height() );
}

QSize
TrackerDelegate :: sizeHint( const QStyleOptionViewItem  & option,
                             const QModelIndex           & index ) const
{
    const TrackerInfo trackerInfo = index.model()->data( index, TrackerModel::TrackerRole ).value<TrackerInfo>();
    return sizeHint( option, trackerInfo );
}

void
TrackerDelegate :: paint( QPainter                    * painter,
                          const QStyleOptionViewItem  & option,
                          const QModelIndex           & index) const
{
    const TrackerInfo trackerInfo = index.model()->data( index, TrackerModel::TrackerRole ).value<TrackerInfo>();
    painter->save( );
    painter->setClipRect( option.rect );
    drawBackground( painter, option, index );
    drawTracker( painter, option, trackerInfo );
    drawFocus(painter, option, option.rect );
    painter->restore( );
}

void
TrackerDelegate :: drawTracker( QPainter                    * painter,
                                const QStyleOptionViewItem  & option,
                                const TrackerInfo           & inf ) const
{
    painter->save( );

    QPixmap icon = inf.st.getFavicon( );
    QRect iconArea( option.rect.x() + myMargin.width(),
                    option.rect.y() + myMargin.height(),
                    icon.width(),
                    icon.height() );
    painter->drawPixmap( iconArea.x(), iconArea.y()+4, icon );

    const int textWidth = option.rect.width() - myMargin.width()*2 - mySpacing - icon.width();
    const int textX = myMargin.width() + icon.width() + mySpacing;
    const QString text = getText( inf );
    QTextDocument textDoc;
    textDoc.setHtml( text );
    const QRect textRect( textX, iconArea.y(), textWidth, option.rect.height() - myMargin.height()*2 );
    painter->translate( textRect.topLeft( ) );
    textDoc.drawContents( painter, textRect.translated( -textRect.topLeft( ) ) );

    painter->restore( );
}

void
TrackerDelegate :: setShowMore( bool b )
{
    myShowMore = b;
}

namespace
{
    QString timeToStringRounded( int seconds )
    {
        if( seconds > 60 ) seconds -= ( seconds % 60 );
        return Formatter::timeToString ( seconds );
    }
}

QString
TrackerDelegate :: getText( const TrackerInfo& inf ) const
{
    QString key;
    QString str;
    const time_t now( time( 0 ) );
    const QString err_markup_begin = "<span style=\"color:red\">";
    const QString err_markup_end = "</span>";
    const QString timeout_markup_begin = "<span style=\"color:#224466\">";
    const QString timeout_markup_end = "</span>";
    const QString success_markup_begin = "<span style=\"color:#008B00\">";
    const QString success_markup_end = "</span>";

    // hostname
    const QString host = Favicons::getHost( QUrl( inf.st.announce ) );
    str += inf.st.isBackup ? "<i>" : "<b>";
    str += host;
    if( !key.isEmpty( ) ) str += " - " + key;
    str += inf.st.isBackup ? "</i>" : "</b>";

    // announce & scrape info
    if( !inf.st.isBackup )
    {
        if( inf.st.hasAnnounced )
        {
            const QString tstr( timeToStringRounded( now - inf.st.lastAnnounceTime ) );
            str += "<br/>\n";
            if( inf.st.lastAnnounceSucceeded )
            {
                str += tr( "Got a list of %1%2 peers%3 %4 ago" )
                           .arg( success_markup_begin )
                           .arg( inf.st.lastAnnouncePeerCount )
                           .arg( success_markup_end )
                           .arg( tstr );
            }
            else if( inf.st.lastAnnounceTimedOut )
            {
                str += tr( "Peer list request timed out %1%2%3 ago; will retry" )
                           .arg( timeout_markup_begin )
                           .arg( tstr )
                           .arg( timeout_markup_end );
            }
            else
            {
                str += tr( "Got an error %1'%2'%3 %4 ago" )
                           .arg( err_markup_begin )
                           .arg( tstr )
                           .arg( err_markup_end )
                           .arg( tstr );
            }
        }

        switch( inf.st.announceState )
        {
            case TR_TRACKER_INACTIVE:
                if( inf.st.hasAnnounced ) {
                    str += "<br/>\n";
                    str += tr( "No updates scheduled" );
                }
                break;

            case TR_TRACKER_WAITING: {
                const QString tstr( timeToStringRounded( inf.st.nextAnnounceTime - now ) );
                str += "<br/>\n";
                str += tr( "Asking for more peers in %1" ).arg( tstr );
                break;
            }

            case TR_TRACKER_QUEUED:
                str += "<br/>\n";
                str += tr( "Queued to ask for more peers" );
                break;

            case TR_TRACKER_ACTIVE: {
                const QString tstr( timeToStringRounded( now - inf.st.lastAnnounceStartTime ) );
                str += "<br/>\n";
                str += tr( "Asking for more peers now... <small>%1</small>" ).arg( tstr );
                break;
            }
        }

        if( myShowMore )
        {
            if( inf.st.hasScraped )
            {
                str += "<br/>\n";
                const QString tstr( timeToStringRounded( now - inf.st.lastScrapeTime ) );
                if( inf.st.lastScrapeSucceeded )
                {
                    str += tr( "Tracker had %1%2 seeders%3 and %4%5 leechers%6 %7 ago" )
                               .arg( success_markup_begin )
                               .arg( inf.st.seederCount )
                               .arg( success_markup_end )
                               .arg( success_markup_begin )
                               .arg( inf.st.leecherCount )
                               .arg( success_markup_end )
                               .arg( tstr );
                }
                else
                {
                    str += tr( "Got a scrape error %1'%2'%3 %4 ago" )
                               .arg( err_markup_begin )
                               .arg( inf.st.lastScrapeResult )
                               .arg( err_markup_end )
                               .arg( tstr );
                }
            }

            switch( inf.st.scrapeState )
            {
                case TR_TRACKER_INACTIVE:
                    break;

                case TR_TRACKER_WAITING: {
                    str += "<br/>\n";
                    const QString tstr( timeToStringRounded( inf.st.nextScrapeTime - now ) );
                    str += tr( "Asking for peer counts in %1" ).arg( tstr );
                    break;
                }

                case TR_TRACKER_QUEUED: {
                    str += "<br/>\n";
                    str += tr( "Queued to ask for peer counts" );
                    break;
                }

                case TR_TRACKER_ACTIVE: {
                    str += "<br/>\n";
                    const QString tstr( timeToStringRounded( now - inf.st.lastScrapeStartTime ) );
                    str += tr( "Asking for peer counts now... <small>%1</small>" ).arg( tstr );
                    break;
                }
            }
        }
    }

    return str;
}

#if 0

    if( inf.isBackup )
        str += "<i>";
    QString announce;
    int announceState;
    int downloadCount;
    bool hasAnnounced; bool hasScraped;
    QString host;
    int id;
    bool isBackup;
    int lastAnnouncePeerCount;
    int lastAnnounceResult;
    int lastAnnounceStartTime;
    bool lastAnnounceSucceeded;
    int lastAnnounceTime;
    bool lastAnnounceTimedOut;
    QString lastScrapeResult;
    int lastScrapeStartTime;
    bool lastScrapeSucceeded;
    int lastScrapeTime;
    bool lastScrapeTimedOut;
    int leecherCount;
    int nextAnnounceTime;
    int nextScrapeTime;
    int scrapeState;
    int seederCount;
    int tier;

}
#endif
