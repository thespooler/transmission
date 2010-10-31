/*
 * This file Copyright (C) Mnemosyne LLC
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation.
 *
 * http://www.gnu.org/licenses/old-licenses/gpl-2.0.html
 *
 * $Id$
 */

#include <cassert>
#include <iostream>

#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFileIconProvider>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHttp>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QList>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QStyle>
#include <QTabWidget>
#include <QTime>
#include <QTimeEdit>
#include <QTimer>
#include <QVBoxLayout>

#include "formatter.h"
#include "hig.h"
#include "prefs.h"
#include "prefs-dialog.h"
#include "session.h"
#include "utils.h"

/***
****
***/

namespace
{
    const char * PREF_KEY( "pref-key" );
};

void
PrefsDialog :: checkBoxToggled( bool checked )
{
    const int key( sender( )->property( PREF_KEY ).toInt( ) );
    setPref( key, checked );
}

QCheckBox *
PrefsDialog :: checkBoxNew( const QString& text, int key )
{
    QCheckBox * box = new QCheckBox( text );
    box->setChecked( myPrefs.getBool( key ) );
    box->setProperty( PREF_KEY, key );
    connect( box, SIGNAL(toggled(bool)), this, SLOT(checkBoxToggled(bool)));
    myWidgets.insert( key, box );
    return box;
}

void
PrefsDialog :: enableBuddyWhenChecked( QCheckBox * box, QWidget * buddy )
{
    connect( box, SIGNAL(toggled(bool)), buddy, SLOT(setEnabled(bool)) );
    buddy->setEnabled( box->isChecked( ) );
}

void
PrefsDialog :: spinBoxChangedIdle( )
{
    const QObject * spin( sender()->property( "SPIN" ).value<QObject*>( ) );
    const int key = spin->property( PREF_KEY ).toInt( );

    const QDoubleSpinBox * d = qobject_cast<const QDoubleSpinBox*>( spin );
    if( d != 0 )
        setPref( key, d->value( ) );
    else
        setPref( key, qobject_cast<const QSpinBox*>(spin)->value( ) );
}

void
PrefsDialog :: spinBoxChanged( int value )
{
    Q_UNUSED( value );

    static const QString timerName( "TIMER_CHILD" );
    QObject * o( sender( ) );

    // user may be spinning through many values, so let's hold off
    // for a moment to kekep from flooding a bunch of prefs changes
    QTimer * timer( o->findChild<QTimer*>( timerName ) );
    if( timer == 0 )
    {
        timer = new QTimer( o );
        timer->setObjectName( timerName );
        timer->setSingleShot( true );
        timer->setProperty( "SPIN", qVariantFromValue( o ) );
        connect( timer, SIGNAL(timeout()), this, SLOT(spinBoxChangedIdle()));
    }
    timer->start( 200 );
}

QSpinBox *
PrefsDialog :: spinBoxNew( int key, int low, int high, int step )
{
    QSpinBox * spin = new QSpinBox( );
    spin->setRange( low, high );
    spin->setSingleStep( step );
    spin->setValue( myPrefs.getInt( key ) );
    spin->setProperty( PREF_KEY, key );
    connect( spin, SIGNAL(valueChanged(int)), this, SLOT(spinBoxChanged(int)));
    myWidgets.insert( key, spin );
    return spin;
}

void
PrefsDialog :: doubleSpinBoxChanged( double value )
{
    Q_UNUSED( value );

    spinBoxChanged( 0 );
}

QDoubleSpinBox *
PrefsDialog :: doubleSpinBoxNew( int key, double low, double high, double step, int decimals )
{
    QDoubleSpinBox * spin = new QDoubleSpinBox( );
    spin->setRange( low, high );
    spin->setSingleStep( step );
    spin->setDecimals( decimals );
    spin->setValue( myPrefs.getDouble( key ) );
    spin->setProperty( PREF_KEY, key );
    connect( spin, SIGNAL(valueChanged(double)), this, SLOT(doubleSpinBoxChanged(double)));
    myWidgets.insert( key, spin );
    return spin;
}

void
PrefsDialog :: timeChanged( const QTime& time )
{
    const int key( sender()->property( PREF_KEY ).toInt( ) );
    const int seconds( QTime().secsTo( time ) );
    setPref( key, seconds / 60 );
}

QTimeEdit*
PrefsDialog :: timeEditNew( int key )
{
    const int minutes( myPrefs.getInt( key ) );
    QTimeEdit * e = new QTimeEdit( );
    e->setDisplayFormat( "hh:mm" );
    e->setProperty( PREF_KEY, key );
    e->setTime( QTime().addSecs( minutes * 60 ) );
    myWidgets.insert( key, e );
    connect( e, SIGNAL(timeChanged(const QTime&)), this, SLOT(timeChanged(const QTime&)) );
    return e;
}

void
PrefsDialog :: textChanged( const QString& text )
{
    const int key( sender()->property( PREF_KEY ).toInt( ) );
    setPref( key, text );
}

QLineEdit*
PrefsDialog :: lineEditNew( int key, int echoMode )
{
    QLineEdit * e = new QLineEdit( myPrefs.getString( key ) );
    e->setProperty( PREF_KEY, key );
    e->setEchoMode( QLineEdit::EchoMode( echoMode ) );
    myWidgets.insert( key, e );
    connect( e, SIGNAL(textChanged(const QString&)), this, SLOT(textChanged(const QString&)) );
    return e;
}

/***
****
***/

QWidget *
PrefsDialog :: createTrackerTab( )
{
    QWidget *l, *r;
    HIG * hig = new HIG( );
    hig->addSectionTitle( tr( "Tracker Proxy" ) );
    hig->addWideControl( l = checkBoxNew( tr( "Connect to tracker via a pro&xy" ), Prefs::PROXY_ENABLED ) );
    myUnsupportedWhenRemote << l;
    l = hig->addRow( tr( "Proxy &server:" ), r = lineEditNew( Prefs::PROXY ) );
    myProxyWidgets << l << r;
    l = hig->addRow( tr( "Proxy &port:" ), r = spinBoxNew( Prefs::PROXY_PORT, 1, 65535, 1 ) );
    myProxyWidgets << l << r;
    hig->addWideControl( l = checkBoxNew( tr( "Use &authentication" ), Prefs::PROXY_AUTH_ENABLED ) );
    myProxyWidgets << l;
    l = hig->addRow( tr( "&Username:" ), r = lineEditNew( Prefs::PROXY_USERNAME ) );
    myProxyAuthWidgets << l << r;
    l = hig->addRow( tr( "Pass&word:" ), r = lineEditNew( Prefs::PROXY_PASSWORD, QLineEdit::Password ) );
    myProxyAuthWidgets << l << r;
    myUnsupportedWhenRemote << myProxyAuthWidgets;
    hig->finish( );
    return hig;
}

/***
****
***/

QWidget *
PrefsDialog :: createWebTab( Session& session )
{
    HIG * hig = new HIG( this );
    hig->addSectionTitle( tr( "Web Client" ) );
    QWidget * w;
    QHBoxLayout * h = new QHBoxLayout( );
    QPushButton * b = new QPushButton( tr( "&Open web client" ) );
    connect( b, SIGNAL(clicked()), &session, SLOT(launchWebInterface()) );
    h->addWidget( b, 0, Qt::AlignRight );
    QWidget * l = checkBoxNew( tr( "&Enable web client" ), Prefs::RPC_ENABLED );
    myUnsupportedWhenRemote << l;
    hig->addRow( l, h, 0 );
    l = hig->addRow( tr( "Listening &port:" ), w = spinBoxNew( Prefs::RPC_PORT, 0, 65535, 1 ) );
    myWebWidgets << l << w;
    hig->addWideControl( w = checkBoxNew( tr( "Use &authentication" ), Prefs::RPC_AUTH_REQUIRED ) );
    myWebWidgets << w;
    l = hig->addRow( tr( "&Username:" ), w = lineEditNew( Prefs::RPC_USERNAME ) );
    myWebAuthWidgets << l << w;
    l = hig->addRow( tr( "Pass&word:" ), w = lineEditNew( Prefs::RPC_PASSWORD, QLineEdit::Password ) );
    myWebAuthWidgets << l << w;
    hig->addWideControl( w = checkBoxNew( tr( "Only allow these IP a&ddresses to connect:" ), Prefs::RPC_WHITELIST_ENABLED ) );
    myWebWidgets << w;
    l = hig->addRow( tr( "Addresses:" ), w = lineEditNew( Prefs::RPC_WHITELIST ) );
    myWebWhitelistWidgets << l << w;
    myUnsupportedWhenRemote << myWebWidgets << myWebAuthWidgets << myWebWhitelistWidgets;
    hig->finish( );
    return hig;
}

/***
****
***/

void
PrefsDialog :: altSpeedDaysEdited( int i )
{
    const int value = qobject_cast<QComboBox*>(sender())->itemData(i).toInt();
    setPref( Prefs::ALT_SPEED_LIMIT_TIME_DAY, value );
}


QWidget *
PrefsDialog :: createSpeedTab( )
{
    QWidget *l, *r;
    HIG * hig = new HIG( this );
    hig->addSectionTitle( tr( "Speed Limits" ) );
    const QString speed_K_str = Formatter::unitStr( Formatter::SPEED, Formatter::KB );

        l = checkBoxNew( tr( "Limit &download speed (%1):" ).arg( speed_K_str ), Prefs::DSPEED_ENABLED );
        r = spinBoxNew( Prefs::DSPEED, 0, INT_MAX, 5 );
        hig->addRow( l, r );
        enableBuddyWhenChecked( qobject_cast<QCheckBox*>(l), r );

        l = checkBoxNew( tr( "Limit &upload speed (%1):" ).arg( speed_K_str ), Prefs::USPEED_ENABLED );
        r = spinBoxNew( Prefs::USPEED, 0, INT_MAX, 5 );
        hig->addRow( l, r );
        enableBuddyWhenChecked( qobject_cast<QCheckBox*>(l), r );

    hig->addSectionDivider( );
    QHBoxLayout * h = new QHBoxLayout;
    h->setSpacing( HIG :: PAD );
    QLabel * label = new QLabel;
    label->setPixmap( QPixmap( ":/icons/alt-limit-off.png" ) );
    label->setAlignment( Qt::AlignLeft|Qt::AlignVCenter );
    h->addWidget( label );
    label = new QLabel( tr( "Temporary Speed Limits" ) );
    label->setStyleSheet( "font: bold" );
    label->setAlignment( Qt::AlignLeft|Qt::AlignVCenter );
    h->addWidget( label );
    hig->addSectionTitle( h );

        QString s = tr( "<small>Override normal speed limits manually or at scheduled times</small>" );
        hig->addWideControl( new QLabel( s ) );

        s = tr( "Limit do&wnload speed (%1):" ).arg( speed_K_str );
        r = spinBoxNew( Prefs :: ALT_SPEED_LIMIT_DOWN, 0, INT_MAX, 5 );
        hig->addRow( s, r );

        s = tr( "Limit u&pload speed (%1):" ).arg( speed_K_str );
        r = spinBoxNew( Prefs :: ALT_SPEED_LIMIT_UP, 0, INT_MAX, 5 );
        hig->addRow( s, r );

        QCheckBox * c = checkBoxNew( tr( "&Scheduled times:" ), Prefs::ALT_SPEED_LIMIT_TIME_ENABLED );
        h = new QHBoxLayout( );
        h->setSpacing( HIG::PAD );
        QWidget * w = timeEditNew( Prefs :: ALT_SPEED_LIMIT_TIME_BEGIN );
        h->addWidget( w, 1 );
        mySchedWidgets << w;
        QLabel * nd = new QLabel( "&to" );
        h->addWidget( nd );
        mySchedWidgets << nd;
        w = timeEditNew( Prefs :: ALT_SPEED_LIMIT_TIME_END );
        nd->setBuddy( w );
        h->addWidget( w, 1 );
        mySchedWidgets << w;
        hig->addRow( c, h, 0 );

        s = tr( "&On days:" );
        QComboBox * box = new QComboBox;
        const QIcon noIcon;
        box->addItem( noIcon, tr( "Every Day" ), QVariant( TR_SCHED_ALL ) );
        box->addItem( noIcon, tr( "Weekdays" ),  QVariant( TR_SCHED_WEEKDAY ) );
        box->addItem( noIcon, tr( "Weekends" ),  QVariant( TR_SCHED_WEEKEND ) );
        box->addItem( noIcon, tr( "Sunday" ),    QVariant( TR_SCHED_SUN ) );
        box->addItem( noIcon, tr( "Monday" ),    QVariant( TR_SCHED_MON ) );
        box->addItem( noIcon, tr( "Tuesday" ),   QVariant( TR_SCHED_TUES ) );
        box->addItem( noIcon, tr( "Wednesday" ), QVariant( TR_SCHED_WED ) );
        box->addItem( noIcon, tr( "Thursday" ),  QVariant( TR_SCHED_THURS ) );
        box->addItem( noIcon, tr( "Friday" ),    QVariant( TR_SCHED_FRI ) );
        box->addItem( noIcon, tr( "Saturday" ),  QVariant( TR_SCHED_SAT ) );
        box->setCurrentIndex( box->findData( myPrefs.getInt( Prefs :: ALT_SPEED_LIMIT_TIME_DAY ) ) );
        connect( box, SIGNAL(activated(int)), this, SLOT(altSpeedDaysEdited(int)) );
        w = hig->addRow( s, box );
        mySchedWidgets << w << box;

    hig->finish( );
    return hig;
}

/***
****
***/

QWidget *
PrefsDialog :: createDesktopTab( )
{
    HIG * hig = new HIG( this );
    hig->addSectionTitle( tr( "Desktop" ) );

    hig->addWideControl( checkBoxNew( tr( "Show Transmission icon in the &notification area" ), Prefs::SHOW_TRAY_ICON ) );
    hig->addWideControl( checkBoxNew( tr( "Show &popup notifications" ), Prefs::SHOW_DESKTOP_NOTIFICATION ) );

    hig->finish( );
    return hig;
}

/***
****
***/

void
PrefsDialog :: onPortTested( bool isOpen )
{
    myPortButton->setEnabled( true );
    myWidgets[Prefs::PEER_PORT]->setEnabled( true );
    myPortLabel->setText( isOpen ? tr( "Port is <b>open</b>" )
                                 : tr( "Port is <b>closed</b>" ) );
}

void
PrefsDialog :: onPortTest( )
{
    myPortLabel->setText( tr( "Testing..." ) );
    myPortButton->setEnabled( false );
    myWidgets[Prefs::PEER_PORT]->setEnabled( false );
    mySession.portTest( );
}

QWidget *
PrefsDialog :: createNetworkTab( )
{
    HIG * hig = new HIG( this );
    hig->addSectionTitle( tr( "Incoming Peers" ) );

    QSpinBox * s = spinBoxNew( Prefs::PEER_PORT, 1, 65535, 1 );
    QHBoxLayout * h = new QHBoxLayout( );
    QPushButton * b = myPortButton = new QPushButton( tr( "Te&st Port" ) );
    QLabel * l = myPortLabel = new QLabel( tr( "Status unknown" ) );
    h->addWidget( l );
    h->addSpacing( HIG :: PAD_BIG );
    h->addWidget( b );
    h->setStretchFactor( l, 1 );
    connect( b, SIGNAL(clicked(bool)), this, SLOT(onPortTest()));
    connect( &mySession, SIGNAL(portTested(bool)), this, SLOT(onPortTested(bool)));

    hig->addRow( tr( "&Port for incoming connections:" ), s );
    hig->addRow( "", h, 0 );
    hig->addWideControl( checkBoxNew( tr( "Pick a &random port every time Transmission is started" ), Prefs :: PEER_PORT_RANDOM_ON_START ) );
    hig->addWideControl( checkBoxNew( tr( "Use UPnP or NAT-PMP port &forwarding from my router" ), Prefs::PORT_FORWARDING ) );

    hig->addSectionDivider( );
    hig->addSectionTitle( tr( "Limits" ) );
    hig->addRow( tr( "Maximum peers per &torrent:" ), spinBoxNew( Prefs::PEER_LIMIT_TORRENT, 1, 300, 5 ) );
    hig->addRow( tr( "Maximum peers &overall:" ), spinBoxNew( Prefs::PEER_LIMIT_GLOBAL, 1, 3000, 5 ) );

    hig->finish( );
    return hig;
}

/***
****
***/

void
PrefsDialog :: onBlocklistDialogDestroyed( QObject * o )
{
    Q_UNUSED( o );

    myBlocklistDialog = 0;
}

void
PrefsDialog :: onUpdateBlocklistCancelled( )
{
    disconnect( &mySession, SIGNAL(blocklistUpdated(int)), this, SLOT(onBlocklistUpdated(int))) ;
    myBlocklistDialog->deleteLater( );
}

void
PrefsDialog :: onBlocklistUpdated( int n )
{
    myBlocklistDialog->setText( tr( "<b>Update succeeded!</b><p>Blocklist now has %Ln rules.", 0, n ) );
    myBlocklistDialog->setTextFormat( Qt::RichText );
}

void
PrefsDialog :: onUpdateBlocklistClicked( )
{
    myBlocklistDialog = new QMessageBox( QMessageBox::Information,
                                         "",
                                         tr( "<b>Update Blocklist</b><p>Getting new blocklist..." ),
                                         QMessageBox::Close,
                                         this );
    connect( myBlocklistDialog, SIGNAL(rejected()), this, SLOT(onUpdateBlocklistCancelled()) );
    connect( &mySession, SIGNAL(blocklistUpdated(int)), this, SLOT(onBlocklistUpdated(int))) ;
    myBlocklistDialog->show( );
    mySession.updateBlocklist( );
}

void
PrefsDialog :: encryptionEdited( int i )
{
    const int value( qobject_cast<QComboBox*>(sender())->itemData(i).toInt( ) );
    setPref( Prefs::ENCRYPTION, value );
}

QWidget *
PrefsDialog :: createPrivacyTab( )
{
    QWidget * w;
    HIG * hig = new HIG( this );

    hig->addSectionTitle( tr( "Blocklist" ) );

    QWidget * l = checkBoxNew( "Enable &blocklist:", Prefs::BLOCKLIST_ENABLED );
    QWidget * e = lineEditNew( Prefs::BLOCKLIST_URL );
    myBlockWidgets << e;
    hig->addRow( l, e );

    l = myBlocklistLabel = new QLabel( "" );
    myBlockWidgets << l;
    w = new QPushButton( tr( "&Update" ) );
    connect( w, SIGNAL(clicked(bool)), this, SLOT(onUpdateBlocklistClicked()));
    myBlockWidgets << w;
    QHBoxLayout * h = new QHBoxLayout( );
    h->addWidget( l );
    h->addStretch( 1 );
    h->addWidget( w );
    hig->addWideControl( h );

    l = checkBoxNew( tr( "Enable &automatic updates" ), Prefs::BLOCKLIST_UPDATES_ENABLED );
    myBlockWidgets << l;
    hig->addWideControl( l );

    hig->addSectionDivider( );
    hig->addSectionTitle( tr( "Privacy" ) );

    QComboBox * box = new QComboBox( );
    box->addItem( tr( "Allow encryption" ), 0 );
    box->addItem( tr( "Prefer encryption" ), 1 );
    box->addItem( tr( "Require encryption" ), 2 );
    myWidgets.insert( Prefs :: ENCRYPTION, box );
    connect( box, SIGNAL(activated(int)), this, SLOT(encryptionEdited(int)));

    hig->addRow( tr( "&Encryption mode:" ), box );
    hig->addWideControl( w = checkBoxNew( tr( "Use PE&X to find more peers" ), Prefs::PEX_ENABLED ) );
    w->setToolTip( tr( "PEX is a tool for exchanging peer lists with the peers you're connected to." ) );
    hig->addWideControl( w = checkBoxNew( tr( "Use &DHT to find more peers" ), Prefs::DHT_ENABLED ) );
    w->setToolTip( tr( "DHT is a tool for finding peers without a tracker." ) );
    hig->addWideControl( w = checkBoxNew( tr( "Use &Local Peer Discovery to find more peers" ), Prefs::LPD_ENABLED ) );
    w->setToolTip( tr( "LPD is a tool for finding peers on your local network." ) );

    hig->finish( );
    updateBlocklistLabel( );
    return hig;
}

/***
****
***/

void
PrefsDialog :: onScriptClicked( void )
{
    const QString title = tr( "Select \"Torrent Done\" Script" );
    const QString myPath = myPrefs.getString( Prefs::SCRIPT_TORRENT_DONE_FILENAME );
    const QString path = Utils::remoteFileChooser( this, title, myPath, false, mySession.isServer() );

    if( !path.isEmpty() )
        onLocationSelected( path, Prefs::SCRIPT_TORRENT_DONE_FILENAME );
}

void
PrefsDialog :: onIncompleteClicked( void )
{
    const QString title = tr( "Select Incomplete Directory" );
    const QString myPath = myPrefs.getString( Prefs::INCOMPLETE_DIR );
    const QString path = Utils::remoteFileChooser( this, title, myPath, true, mySession.isServer() );

    if( !path.isEmpty() )
        onLocationSelected( path, Prefs::INCOMPLETE_DIR );
}

void
PrefsDialog :: onWatchClicked( void )
{
    const QString title = tr( "Select Watch Directory" );
    const QString myPath = myPrefs.getString( Prefs::DIR_WATCH );
    const QString path = Utils::remoteFileChooser( this, title, myPath, true, true );

    if( !path.isEmpty() )
        onLocationSelected( path, Prefs::DIR_WATCH );
}

void
PrefsDialog :: onDestinationClicked( void )
{
    const QString title = tr( "Select Destination" );
    const QString myPath = myPrefs.getString( Prefs::DOWNLOAD_DIR );
    const QString path = Utils::remoteFileChooser( this, title, myPath, true, mySession.isServer() );

    if( !path.isEmpty() )
        onLocationSelected( path, Prefs::DOWNLOAD_DIR );
}

void
PrefsDialog :: onLocationSelected( const QString& path, int key )
{
    setPref( key, path );
}

QWidget *
PrefsDialog :: createTorrentsTab( )
{
    const int iconSize( style( )->pixelMetric( QStyle :: PM_SmallIconSize ) );
    const QFileIconProvider iconProvider;
    const QIcon folderIcon = iconProvider.icon( QFileIconProvider::Folder );
    const QPixmap folderPixmap = folderIcon.pixmap( iconSize );
    const QIcon fileIcon = iconProvider.icon( QFileIconProvider::File );
    const QPixmap filePixmap = fileIcon.pixmap( iconSize );

    QWidget *l, *r;
    HIG * hig = new HIG( this );
    hig->addSectionTitle( tr( "Adding" ) );

        l = checkBoxNew( tr( "Automatically &add torrents from:" ), Prefs::DIR_WATCH_ENABLED );
        QPushButton * b = myWatchButton = new QPushButton;
        b->setIcon( folderPixmap );
        b->setStyleSheet( "text-align: left; padding-left: 5; padding-right: 5" );
        connect( b, SIGNAL(clicked(bool)), this, SLOT(onWatchClicked(void)) );
        hig->addRow( l, b );
        enableBuddyWhenChecked( qobject_cast<QCheckBox*>(l), b );

        hig->addWideControl( checkBoxNew( tr( "Show &options dialog" ), Prefs::OPTIONS_PROMPT ) );
        hig->addWideControl( checkBoxNew( tr( "&Start when added" ), Prefs::START ) );
        hig->addWideControl( checkBoxNew( tr( "Mo&ve .torrent file to the trash" ), Prefs::TRASH_ORIGINAL ) );

    hig->addSectionDivider( );
    hig->addSectionTitle( tr( "Downloading" ) );

        hig->addWideControl( checkBoxNew( tr( "Append \".&part\" to incomplete files' names" ), Prefs::RENAME_PARTIAL_FILES ) );

        b = myDestinationButton = new QPushButton;
        b->setIcon( folderPixmap );
        b->setStyleSheet( "text-align: left; padding-left: 5; padding-right: 5" );
        connect( b, SIGNAL(clicked(bool)), this, SLOT(onDestinationClicked(void)) );
        hig->addRow( tr( "Save to &Location:" ), b );

        l = myIncompleteCheckbox = checkBoxNew( tr( "Keep &incomplete files in:" ), Prefs::INCOMPLETE_DIR_ENABLED );
        b = myIncompleteButton = new QPushButton;
        b->setIcon( folderPixmap );
        b->setStyleSheet( "text-align: left; padding-left: 5; padding-right: 5" );
        connect( b, SIGNAL(clicked(bool)), this, SLOT(onIncompleteClicked(void)) );
        hig->addRow( myIncompleteCheckbox, b );
        enableBuddyWhenChecked( qobject_cast<QCheckBox*>(l), b );

        l = myTorrentDoneScriptCheckbox = checkBoxNew( tr( "Call scrip&t when torrent is completed:" ), Prefs::SCRIPT_TORRENT_DONE_ENABLED );
        b = myTorrentDoneScriptButton = new QPushButton;
        b->setIcon( filePixmap );
        b->setStyleSheet( "text-align: left; padding-left: 5; padding-right: 5" );
        connect( b, SIGNAL(clicked(bool)), this, SLOT(onScriptClicked(void)) );
        hig->addRow( myTorrentDoneScriptCheckbox, b );
        enableBuddyWhenChecked( qobject_cast<QCheckBox*>(l), b );

    hig->addSectionDivider( );
    hig->addSectionTitle( tr( "Seeding Limits" ) );

        l = checkBoxNew( tr( "Stop seeding at &ratio:" ), Prefs::RATIO_ENABLED );
        r = doubleSpinBoxNew( Prefs::RATIO, 0, INT_MAX, 0.5, 2 );
        hig->addRow( l, r );
        enableBuddyWhenChecked( qobject_cast<QCheckBox*>(l), r );

        l = checkBoxNew( tr( "Stop seeding if idle for &N minutes:" ), Prefs::IDLE_LIMIT_ENABLED );
        r = spinBoxNew( Prefs::IDLE_LIMIT, 1, INT_MAX, 5 );
        hig->addRow( l, r );
        enableBuddyWhenChecked( qobject_cast<QCheckBox*>(l), r );

    hig->finish( );
    return hig;
}

/***
****
***/

PrefsDialog :: PrefsDialog( Session& session, Prefs& prefs, QWidget * parent ):
    QDialog( parent ),
    myIsServer( session.isServer( ) ),
    mySession( session ),
    myPrefs( prefs ),
    myLayout( new QVBoxLayout( this ) )
{
    setWindowTitle( tr( "Transmission Preferences" ) );

    QTabWidget * t = new QTabWidget( this );
    t->addTab( createTorrentsTab( ),     tr( "Torrents" ) );
    t->addTab( createSpeedTab( ),        tr( "Speed" ) );
    t->addTab( createPrivacyTab( ),      tr( "Privacy" ) );
    t->addTab( createNetworkTab( ),      tr( "Network" ) );
    t->addTab( createDesktopTab( ),      tr( "Desktop" ) );
    t->addTab( createWebTab( session ),  tr( "Web" ) );
    //t->addTab( createTrackerTab( ),    tr( "Trackers" ) );
    myLayout->addWidget( t );

    QDialogButtonBox * buttons = new QDialogButtonBox( QDialogButtonBox::Close, Qt::Horizontal, this );
    connect( buttons, SIGNAL(rejected()), this, SLOT(close()) ); // "close" triggers rejected
    myLayout->addWidget( buttons );
    QWidget::setAttribute( Qt::WA_DeleteOnClose, true );

    connect( &mySession, SIGNAL(sessionUpdated()), this, SLOT(sessionUpdated()));

    QList<int> keys;
    keys << Prefs :: RPC_ENABLED
         << Prefs :: PROXY_ENABLED
         << Prefs :: ALT_SPEED_LIMIT_ENABLED
         << Prefs :: ALT_SPEED_LIMIT_TIME_ENABLED
         << Prefs :: ENCRYPTION
         << Prefs :: BLOCKLIST_ENABLED
         << Prefs :: DIR_WATCH
         << Prefs :: DOWNLOAD_DIR
         << Prefs :: INCOMPLETE_DIR
         << Prefs :: INCOMPLETE_DIR_ENABLED;
    foreach( int key, keys )
        refreshPref( key );

    // if it's a remote session, disable the preferences
    // that don't work in remote sessions
    if( !myIsServer )  {
        foreach( QWidget * w, myUnsupportedWhenRemote ) {
            w->setToolTip( tr( "Not supported by remote sessions" ) );
            w->setEnabled( false );
        }
    }
}

PrefsDialog :: ~PrefsDialog( )
{
}

void
PrefsDialog :: setPref( int key, const QVariant& v )
{
    myPrefs.set( key, v );
    refreshPref( key );
}

/***
****
***/

void
PrefsDialog :: sessionUpdated( )
{
    updateBlocklistLabel( );
}

void
PrefsDialog :: updateBlocklistLabel( )
{
    const int n = mySession.blocklistSize( );
    myBlocklistLabel->setText( tr( "<i>Blocklist contains %Ln rules)", 0, n ) );
}

void
PrefsDialog :: refreshPref( int key )
{
    switch( key )
    {
        case Prefs :: RPC_ENABLED:
        case Prefs :: RPC_WHITELIST_ENABLED:
        case Prefs :: RPC_AUTH_REQUIRED: {
            const bool enabled( myPrefs.getBool( Prefs::RPC_ENABLED ) );
            const bool whitelist( myPrefs.getBool( Prefs::RPC_WHITELIST_ENABLED ) );
            const bool auth( myPrefs.getBool( Prefs::RPC_AUTH_REQUIRED ) );
            foreach( QWidget * w, myWebWhitelistWidgets ) w->setEnabled( enabled && whitelist );
            foreach( QWidget * w, myWebAuthWidgets ) w->setEnabled( enabled && auth );
            foreach( QWidget * w, myWebWidgets ) w->setEnabled( enabled );
            break;
        }

        case Prefs :: PROXY_ENABLED:
        case Prefs :: PROXY_AUTH_ENABLED: {
            const bool enabled( myPrefs.getBool( Prefs::PROXY_ENABLED ) );
            const bool auth( myPrefs.getBool( Prefs::PROXY_AUTH_ENABLED ) );
            foreach( QWidget * w, myProxyAuthWidgets ) w->setEnabled( enabled && auth );
            foreach( QWidget * w, myProxyWidgets ) w->setEnabled( enabled );
            break;
        }

        case Prefs :: ALT_SPEED_LIMIT_TIME_ENABLED: {
            const bool enabled = myPrefs.getBool( key );
            foreach( QWidget * w, mySchedWidgets ) w->setEnabled( enabled );
            break;
        }

        case Prefs :: BLOCKLIST_ENABLED: {
            const bool enabled = myPrefs.getBool( key );
            foreach( QWidget * w, myBlockWidgets ) w->setEnabled( enabled );
            break;
        }

        case Prefs :: DIR_WATCH:
            myWatchButton->setText( QFileInfo(myPrefs.getString(Prefs::DIR_WATCH)).fileName() );
            break;

        case Prefs :: PEER_PORT:
            myPortLabel->setText( tr( "Status unknown" ) );
            myPortButton->setEnabled( true );
            break;

        case Prefs :: DOWNLOAD_DIR: {
            QString path( myPrefs.getString( key ) );
            myDestinationButton->setText( QFileInfo(path).fileName() );
            break;
        }

        case Prefs :: INCOMPLETE_DIR: {
            QString path( myPrefs.getString( key ) );
            myIncompleteButton->setText( QFileInfo(path).fileName() );
            break;
        }

        case Prefs :: INCOMPLETE_DIR_ENABLED: {
            const bool enabled = myPrefs.getBool( key );
            myIncompleteButton->setEnabled( enabled );
            break;
        }

        default:
            break;
    }

    key2widget_t::iterator it( myWidgets.find( key ) );
    if( it != myWidgets.end( ) )
    {
        QWidget * w( it.value( ) );
        QCheckBox * checkBox;
        QSpinBox * spin;
        QDoubleSpinBox * doubleSpin;
        QTimeEdit * timeEdit;
        QLineEdit * lineEdit;

        if(( checkBox = qobject_cast<QCheckBox*>(w)))
        {
            checkBox->setChecked( myPrefs.getBool( key ) );
        }
        else if(( spin = qobject_cast<QSpinBox*>(w)))
        {
            spin->setValue( myPrefs.getInt( key ) );
        }
        else if(( doubleSpin = qobject_cast<QDoubleSpinBox*>(w)))
        {
            doubleSpin->setValue( myPrefs.getDouble( key ) );
        }
        else if(( timeEdit = qobject_cast<QTimeEdit*>(w)))
        {
            const int minutes( myPrefs.getInt( key ) );
            timeEdit->setTime( QTime().addSecs( minutes * 60 ) );
        }
        else if(( lineEdit = qobject_cast<QLineEdit*>(w)))
        {
            lineEdit->setText( myPrefs.getString( key ) );
        }
        else if( key == Prefs::ENCRYPTION )
        {
            QComboBox * comboBox( qobject_cast<QComboBox*>( w ) );
            const int index = comboBox->findData( myPrefs.getInt( key ) );
            comboBox->setCurrentIndex( index );
        }
    }
}

bool
PrefsDialog :: isAllowed( int key ) const
{
    Q_UNUSED( key );

    return true;
}
