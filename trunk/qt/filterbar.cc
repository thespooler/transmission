/*
 * This file Copyright (C) 2010 Mnemosyne LLC
 *
 * This file is licensed by the GPL version 2.  Works owned by the
 * Transmission project are granted a special exemption to clause 2(b)
 * so that the bulk of its code can remain under the MIT license.
 * This exemption does not extend to derived works not owned by
 * the Transmission project.
 *
 * $Id$
 */

#include <QString>
#include <QtGui>

#include "app.h"
#include "favicon.h"
#include "filters.h"
#include "filterbar.h"
#include "prefs.h"
#include "qticonloader.h"
#include "torrent-filter.h"
#include "torrent-model.h"
#include "utils.h"

/****
*****
*****  DELEGATE
*****
****/

enum
{
    TorrentCountRole = Qt::UserRole + 1,
    ActivityRole,
    TrackerRole
};

FilterBarComboBoxDelegate :: FilterBarComboBoxDelegate( QObject * parent, QComboBox * combo ):
    QItemDelegate( parent ),
    myCombo( combo )
{
}

bool
FilterBarComboBoxDelegate :: isSeparator( const QModelIndex &index )
{
    return index.data(Qt::AccessibleDescriptionRole).toString() == QLatin1String("separator");
}
void
FilterBarComboBoxDelegate :: setSeparator( QAbstractItemModel * model, const QModelIndex& index )
{
    model->setData( index, QString::fromLatin1("separator"), Qt::AccessibleDescriptionRole );

    if( QStandardItemModel *m = qobject_cast<QStandardItemModel*>(model) )
       if (QStandardItem *item = m->itemFromIndex(index))
           item->setFlags(item->flags() & ~(Qt::ItemIsSelectable|Qt::ItemIsEnabled));
}

void
FilterBarComboBoxDelegate :: paint( QPainter                    * painter,
                                    const QStyleOptionViewItem  & option,
                                    const QModelIndex           & index ) const
{
    if( isSeparator( index ) )
    {
        QRect rect = option.rect;
        if (const QStyleOptionViewItemV3 *v3 = qstyleoption_cast<const QStyleOptionViewItemV3*>(&option))
            if (const QAbstractItemView *view = qobject_cast<const QAbstractItemView*>(v3->widget))
                rect.setWidth(view->viewport()->width());
        QStyleOption opt;
        opt.rect = rect;
        myCombo->style()->drawPrimitive(QStyle::PE_IndicatorToolBarSeparator, &opt, painter, myCombo);
    }
    else
    {
        QStyleOptionViewItem disabledOption = option;
        disabledOption.state &= ~( QStyle::State_Enabled | QStyle::State_Selected );
        QRect boundingBox = option.rect;

        const int hmargin = myCombo->style()->pixelMetric( QStyle::PM_LayoutHorizontalSpacing, 0, myCombo );
        boundingBox.setLeft( boundingBox.left() + hmargin );
        boundingBox.setRight( boundingBox.right() - hmargin );

        QRect decorationRect = rect( option, index, Qt::DecorationRole );
        decorationRect.moveLeft( decorationRect.left( ) );
        decorationRect.setSize( myCombo->iconSize( ) );
        decorationRect = QStyle::alignedRect( Qt::LeftToRight,
                                              Qt::AlignLeft|Qt::AlignVCenter,
                                              decorationRect.size(), boundingBox );
        boundingBox.setLeft( decorationRect.right() + hmargin );

        QRect countRect  = rect( option, index, TorrentCountRole );
        countRect = QStyle::alignedRect( Qt::LeftToRight,
                                         Qt::AlignRight|Qt::AlignVCenter,
                                         countRect.size(), boundingBox );
        boundingBox.setRight( countRect.left() - hmargin );
        const QRect displayRect = boundingBox;

        drawBackground( painter, option, index );
        QStyleOptionViewItem option2 = option;
        option2.decorationSize = myCombo->iconSize( );
        drawDecoration( painter, option, decorationRect, decoration(option2,index.data(Qt::DecorationRole)) );
        drawDisplay( painter, option, displayRect, index.data(Qt::DisplayRole).toString() );
        drawDisplay( painter, disabledOption, countRect, index.data(TorrentCountRole).toString() );
        drawFocus( painter, option, displayRect|countRect );
    }
}

QSize
FilterBarComboBoxDelegate :: sizeHint( const QStyleOptionViewItem & option,
                                       const QModelIndex          & index ) const
{
    if( isSeparator( index ) )
    {
        const int pm = myCombo->style()->pixelMetric(QStyle::PM_DefaultFrameWidth, 0, myCombo);
        return QSize( pm, pm + 10 );
    }
    else
    {
        QStyle * s = myCombo->style( );
        const int hmargin = s->pixelMetric( QStyle::PM_LayoutHorizontalSpacing, 0, myCombo );


        QSize size = QItemDelegate::sizeHint( option, index );
        size.setHeight( qMax( size.height(), myCombo->iconSize().height() + 6 ) );
        size.rwidth() += s->pixelMetric( QStyle::PM_FocusFrameHMargin, 0, myCombo );
        size.rwidth() += rect(option,index,TorrentCountRole).width();
        size.rwidth() += hmargin * 4;
        return size;
    }
}

/**
***
**/

FilterBarComboBox :: FilterBarComboBox( QWidget * parent ):
    QComboBox( parent )
{
}

void
FilterBarComboBox :: paintEvent( QPaintEvent * e )
{
    Q_UNUSED( e );

    QStylePainter painter(this);
    painter.setPen(palette().color(QPalette::Text));

    // draw the combobox frame, focusrect and selected etc.
    QStyleOptionComboBox opt;
    initStyleOption(&opt);
    painter.drawComplexControl(QStyle::CC_ComboBox, opt);

    // draw the icon and text
    const QModelIndex modelIndex = model()->index( currentIndex(), 0, rootModelIndex() );
    if( modelIndex.isValid( ) )
    {
        QStyle * s = style();
        QRect rect = s->subControlRect( QStyle::CC_ComboBox, &opt, QStyle::SC_ComboBoxEditField, this );
        const int hmargin = s->pixelMetric( QStyle::PM_LayoutHorizontalSpacing, 0, this );
        rect.setRight( rect.right() - hmargin );

        // draw the icon
        QPixmap pixmap;
        QVariant variant = modelIndex.data( Qt::DecorationRole );
        switch( variant.type( ) ) {
            case QVariant::Pixmap: pixmap = qvariant_cast<QPixmap>(variant); break;
            case QVariant::Icon:   pixmap = qvariant_cast<QIcon>(variant).pixmap(iconSize()); break;
            default: break;
        }
        if( !pixmap.isNull() ) {
            s->drawItemPixmap( &painter, rect, Qt::AlignLeft|Qt::AlignVCenter, pixmap );
            rect.setLeft( rect.left() + pixmap.width() + hmargin );
        }

        // draw the count
        const int count = modelIndex.data( TorrentCountRole ).toInt();
        if( count >= 0 ) {
            const QString text = QString::number( count);
            const QPen pen = painter.pen( );
            painter.setPen( opt.palette.color( QPalette::Disabled, QPalette::Text ) );
            QRect r = s->itemTextRect( painter.fontMetrics(), rect, Qt::AlignRight|Qt::AlignVCenter, false, text );
            painter.drawText( r, 0, text );
            rect.setRight( r.left() - hmargin );
            painter.setPen( pen );
        }

        // draw the text
        QString text = modelIndex.data( Qt::DisplayRole ).toString();
        text = painter.fontMetrics().elidedText ( text, Qt::ElideRight, rect.width() );
        s->drawItemText( &painter, rect, Qt::AlignLeft|Qt::AlignVCenter, opt.palette, true, text );
    }
}

/****
*****
*****  ACTIVITY
*****
****/

QComboBox*
FilterBar :: createActivityCombo( )
{
    QComboBox * c = new FilterBarComboBox( this );
    FilterBarComboBoxDelegate * delegate = new FilterBarComboBoxDelegate( 0, c );
    c->setItemDelegate( delegate );

    QPixmap blankPixmap( c->iconSize( ) );
    blankPixmap.fill( Qt::transparent );
    QIcon blankIcon( blankPixmap );

    QStandardItemModel * model = new QStandardItemModel;

    QStandardItem * row = new QStandardItem( tr( "All" ) );
    row->setData( FilterMode::SHOW_ALL, ActivityRole );
    model->appendRow( row );

    model->appendRow( new QStandardItem ); // separator
    delegate->setSeparator( model, model->index( 1, 0 ) );

    row = new QStandardItem( QtIconLoader::icon( "system-run" ), tr( "Active" ) );
    row->setData( FilterMode::SHOW_ACTIVE, ActivityRole );
    model->appendRow( row );

    row = new QStandardItem( QtIconLoader::icon( "go-down" ), tr( "Downloading" ) );
    row->setData( FilterMode::SHOW_DOWNLOADING, ActivityRole );
    model->appendRow( row );

    row = new QStandardItem( QtIconLoader::icon( "go-up" ), tr( "Seeding" ) );
    row->setData( FilterMode::SHOW_SEEDING, ActivityRole );
    model->appendRow( row );

    row = new QStandardItem( QtIconLoader::icon( "media-playback-pause", blankIcon ), tr( "Paused" ) );
    row->setData( FilterMode::SHOW_PAUSED, ActivityRole );
    model->appendRow( row );

    row = new QStandardItem( blankIcon, tr( "Queued" ) );
    row->setData( FilterMode::SHOW_QUEUED, ActivityRole );
    model->appendRow( row );

    row = new QStandardItem( QtIconLoader::icon( "view-refresh", blankIcon ), tr( "Verifying" ) );
    row->setData( FilterMode::SHOW_VERIFYING, ActivityRole );
    model->appendRow( row );

    row = new QStandardItem( QtIconLoader::icon( "dialog-error", blankIcon ), tr( "Error" ) );
    row->setData( FilterMode::SHOW_ERROR, ActivityRole );
    model->appendRow( row );

    c->setModel( model );
    return c;
}

/****
*****
*****  
*****
****/

namespace
{
    QString readableHostName( const QString host )
    {
        // get the readable name...
        QString name = host;
        const int pos = name.lastIndexOf( '.' );
        if( pos >= 0 )
            name.truncate( pos );
        if( !name.isEmpty( ) )
            name[0] = name[0].toUpper( );
        return name;
    }
}

void
FilterBar :: refreshTrackers( )
{
    Favicons& favicons = dynamic_cast<MyApp*>(QApplication::instance())->favicons;
    const int firstTrackerRow = 2; // skip over the "All" and separator...

    // pull info from the tracker model...
    QSet<QString> oldHosts;
    for( int row=firstTrackerRow; ; ++row ) {
        QModelIndex index = myTrackerModel->index( row, 0 );
        if( !index.isValid( ) )
            break;
        oldHosts << index.data(TrackerRole).toString();
    }

    // pull the new stats from the torrent model...
    QSet<QString> newHosts;
    QMap<QString,int> torrentsPerHost;
    for( int row=0; ; ++row )
    {
        QModelIndex index = myTorrents.index( row, 0 );
        if( !index.isValid( ) )
            break;
        const Torrent * tor = index.data( TorrentModel::TorrentRole ).value<const Torrent*>();
        const QStringList trackers = tor->trackers( );
        QSet<QString> torrentHosts;
        foreach( QString tracker, trackers )
            torrentHosts.insert( Favicons::getHost( QUrl( tracker ) ) );
        foreach( QString host, torrentHosts ) {
            newHosts.insert( host );
            ++torrentsPerHost[host];
        }
    }

    // update the "All" row
    myTrackerModel->setData( myTrackerModel->index(0,0), myTorrents.rowCount(), TorrentCountRole );

    // rows to update
    foreach( QString host, oldHosts & newHosts )
    {
        const QString name = readableHostName( host );
        QStandardItem * row = myTrackerModel->findItems(name).front();
        row->setData( torrentsPerHost[host], TorrentCountRole );
        row->setData( favicons.findFromHost(host), Qt::DecorationRole );
    }

    // rows to remove
    foreach( QString host, oldHosts - newHosts ) {
        const QString name = readableHostName( host );
        QStandardItem * item = myTrackerModel->findItems(name).front();
        if( !item->data(TrackerRole).toString().isEmpty() ) // don't remove "All"
            myTrackerModel->removeRows( item->row(), 1 );
    }

    // rows to add
    bool anyAdded = false;
    foreach( QString host, newHosts - oldHosts )
    {
        const QString name = readableHostName( host );

        // find the sorted position to add this row
        int i = firstTrackerRow;
        for( int n=myTrackerModel->rowCount(); i<n; ++i )
            if( myTrackerModel->index(i,0).data(Qt::DisplayRole).toString() > name )
                break;

        // add the row
        QStandardItem * row = new QStandardItem( favicons.findFromHost( host ), readableHostName( host ) );
        row->setData( torrentsPerHost[host], TorrentCountRole );
        row->setData( favicons.findFromHost(host), Qt::DecorationRole );
        row->setData( host, TrackerRole );
        myTrackerModel->insertRow( i, row );
        anyAdded = true;
    }

    if( anyAdded ) // the one added might match our filter...
        refreshPref( Prefs::FILTER_TRACKERS );
}


QComboBox*
FilterBar :: createTrackerCombo( QStandardItemModel * model )
{
    QComboBox * c = new FilterBarComboBox( this );
    FilterBarComboBoxDelegate * delegate = new FilterBarComboBoxDelegate( 0, c );
    c->setItemDelegate( delegate );

    QStandardItem * row = new QStandardItem( tr( "All" ) );
    row->setData( "", TrackerRole );
    row->setData( myTorrents.rowCount(), TorrentCountRole );
    model->appendRow( row );

    model->appendRow( new QStandardItem ); // separator
    delegate->setSeparator( model, model->index( 1, 0 ) );

    c->setModel( model );
    return c;
}

/****
*****
*****  
*****
****/

FilterBar :: FilterBar( Prefs& prefs, TorrentModel& torrents, TorrentFilter& filter, QWidget * parent ):
    QWidget( parent ),
    myPrefs( prefs ),
    myTorrents( torrents ),
    myFilter( filter ),
    myRecountTimer( new QTimer( this ) ),
    myIsBootstrapping( true )
{
    QHBoxLayout * h = new QHBoxLayout( this );
    int hmargin = style()->pixelMetric( QStyle::PM_LayoutHorizontalSpacing );

    h->setSpacing( 0 );
    h->setContentsMargins( 2, 2, 2, 2 );
    h->addWidget( new QLabel( tr( "Show:" ), this ) );
    h->addSpacing( hmargin );

    myActivityCombo = createActivityCombo( );
    h->addWidget( myActivityCombo, 1 );
    h->addSpacing( hmargin );

    myTrackerModel = new QStandardItemModel;
    myTrackerCombo = createTrackerCombo( myTrackerModel );
    h->addWidget( myTrackerCombo, 1 );
    h->addSpacing( hmargin*2 );
    
    myLineEdit = new QLineEdit( this );
    h->addWidget( myLineEdit );
    connect( myLineEdit, SIGNAL(textChanged(QString)), this, SLOT(onTextChanged(QString)));

    QPushButton * p = new QPushButton;
    QIcon icon = QtIconLoader::icon( "edit-clear" );
    if( icon.isNull( ) )
        icon = style()->standardIcon( QStyle::SP_DialogCloseButton );
    int iconSize = style()->pixelMetric( QStyle::PM_SmallIconSize );
    p->setIconSize( QSize( iconSize, iconSize ) );
    p->setIcon( icon );
    p->setFlat( true );
    h->addWidget( p );
    connect( p, SIGNAL(clicked(bool)), myLineEdit, SLOT(clear()));

    // listen for changes from the other players
    connect( &myPrefs, SIGNAL(changed(int)), this, SLOT(refreshPref(int)));
    connect( myActivityCombo, SIGNAL(currentIndexChanged(int)), this, SLOT(onActivityIndexChanged(int)));
    connect( myTrackerCombo, SIGNAL(currentIndexChanged(int)), this, SLOT(onTrackerIndexChanged(int)));
    connect( &myTorrents, SIGNAL(modelReset()), this, SLOT(onTorrentModelReset()));
    connect( &myTorrents, SIGNAL(rowsInserted(const QModelIndex&,int,int)), this, SLOT(onTorrentModelRowsInserted(const QModelIndex&,int,int)));
    connect( &myTorrents, SIGNAL(rowsRemoved(const QModelIndex&,int,int)), this, SLOT(onTorrentModelRowsRemoved(const QModelIndex&,int,int)));
    connect( &myTorrents, SIGNAL(dataChanged(const QModelIndex&,const QModelIndex&)), this, SLOT(onTorrentModelDataChanged(const QModelIndex&,const QModelIndex&)));
    connect( myRecountTimer, SIGNAL(timeout()), this, SLOT(recount()) );

    recountSoon( );
    refreshTrackers( );
    myIsBootstrapping = false;

    // initialize our state
    QList<int> initKeys;
    initKeys << Prefs :: FILTER_MODE
             << Prefs :: FILTER_TRACKERS;
    foreach( int key, initKeys )
        refreshPref( key );
}

FilterBar :: ~FilterBar( )
{
    delete myRecountTimer;
}

/***
****
***/

void
FilterBar :: refreshPref( int key )
{
    switch( key )
    {
        case Prefs :: FILTER_MODE: {
            const FilterMode m = myPrefs.get<FilterMode>( key );
            QAbstractItemModel * model = myActivityCombo->model( );
            QModelIndexList indices = model->match( model->index(0,0), ActivityRole, m.mode(), -1 );
            myActivityCombo->setCurrentIndex( indices.isEmpty() ? 0 : indices.first().row( ) );
            break;
        }

        case Prefs :: FILTER_TRACKERS: {
            const QString tracker = myPrefs.getString( key );
            QList<QStandardItem*> rows = myTrackerModel->findItems( tracker );
            if( !rows.isEmpty( )  )
                myTrackerCombo->setCurrentIndex( rows.first()->row() );
            else if( myTorrents.rowCount( ) > 0 ) // uh-oh... we don't have this tracker anymore.  best use "show all" as a fallback
                myPrefs.set( key, "" );
            break;
        }

        case Prefs :: FILTER_TEXT:
            myLineEdit->setText( myPrefs.getString( key ) );
            break;
    }
}

void
FilterBar :: onTextChanged( const QString& str )
{
    if( !myIsBootstrapping )
        myPrefs.set( Prefs::FILTER_TEXT, str.trimmed( ) );
}

void
FilterBar :: onTrackerIndexChanged( int i )
{
    if( !myIsBootstrapping )
        myPrefs.set( Prefs::FILTER_TRACKERS, myTrackerCombo->itemData( i, Qt::DisplayRole ).toString( ) );
}

void
FilterBar :: onActivityIndexChanged( int i )
{
    if( !myIsBootstrapping )
    {
        const FilterMode mode = myActivityCombo->itemData( i, ActivityRole ).toInt( );
        myPrefs.set( Prefs::FILTER_MODE, mode );
    }
}

/***
****
***/

void FilterBar :: onTorrentModelReset( ) { recountSoon( ); }
void FilterBar :: onTorrentModelRowsInserted( const QModelIndex&, int, int ) { recountSoon( ); }
void FilterBar :: onTorrentModelRowsRemoved( const QModelIndex&, int, int ) { recountSoon( ); }
void FilterBar :: onTorrentModelDataChanged( const QModelIndex&, const QModelIndex& ) { recountSoon( ); }

void
FilterBar :: recountSoon( )
{
    if( !myRecountTimer->isActive( ) )
    {
        myRecountTimer->setSingleShot( true );
        myRecountTimer->start( 500 );
    }
}
void
FilterBar :: recount ( )
{
    // recount the activity combobox...
    for( int i=0, n=FilterMode::NUM_MODES; i<n; ++i )
    {
        const FilterMode m( i );
        QAbstractItemModel * model = myActivityCombo->model( );
        QModelIndexList indices = model->match( model->index(0,0), ActivityRole, m.mode(), -1 );
        if( !indices.isEmpty( ) ) {
            const int count = myFilter.count( m );
            model->setData( indices.first(), count, TorrentCountRole );
        }
    }

    refreshTrackers( );
}
