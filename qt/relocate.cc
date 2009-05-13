/*
 * This file Copyright (C) 2009 Charles Kerr <charles@transmissionbt.com>
 *
 * This file is licensed by the GPL version 2.  Works owned by the
 * Transmission project are granted a special exemption to clause 2(b)
 * so that the bulk of its code can remain under the MIT license.
 * This exemption does not extend to derived works not owned by
 * the Transmission project.
 *
 * $Id:$
 */

#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <QRadioButton>
#include <QDir>
#include <QFileDialog>
#include <QSet>
#include <QDialogButtonBox>
#include <QWidget>

#include "hig.h"
#include "relocate.h"
#include "session.h"

QString RelocateDialog :: myPath;

bool RelocateDialog :: myMoveFlag = true;

void
RelocateDialog :: onSetLocation( )
{
    QSet<int> ids;
    ids << myTorrentId;
    mySession.torrentSetLocation( ids, myPath, myMoveFlag );
    deleteLater( );
}

void
RelocateDialog :: onFileSelected( const QString& path )
{
    myPath = path;
    myDirButton->setText( myPath );
}

void
RelocateDialog :: onDirButtonClicked( )
{
    QFileDialog * d = new QFileDialog( this );
    d->setFileMode( QFileDialog::Directory );
    d->selectFile( myPath );
    d->show( );
    connect( d, SIGNAL(fileSelected(const QString&)), this, SLOT(onFileSelected(const QString&)));
}

void
RelocateDialog :: onMoveToggled( bool b )
{
    myMoveFlag = b;
}

RelocateDialog :: RelocateDialog( Session& session, int torrentId, QWidget * parent ):
    QDialog( parent ),
    mySession( session ),
    myTorrentId( torrentId )
{
    QRadioButton * find_rb;
    setWindowTitle( tr( "Set Torrent Location" ) );

    if( myPath.isEmpty( ) )
        myPath = QDir::homePath( );

    HIG * hig = new HIG( );
    hig->addSectionTitle( tr( "Location" ) );
    hig->addRow( tr( "Torrent &location:" ), myDirButton = new QPushButton( myPath ) );
    hig->addWideControl( myMoveRadio = new QRadioButton( tr( "&Move from the current folder" ), this ) );
    hig->addWideControl( find_rb = new QRadioButton( tr( "Local data is &already there" ), this ) );
    hig->finish( );

    if( myMoveFlag )
        myMoveRadio->setChecked( true );
    else
        find_rb->setChecked( true );

    connect( myMoveRadio, SIGNAL(toggled(bool)), this, SLOT(onMoveToggled(bool)));
    connect( myDirButton, SIGNAL(clicked(bool)), this, SLOT(onDirButtonClicked()));

    QLayout * layout = new QVBoxLayout( this );
    layout->addWidget( hig );
    QDialogButtonBox * buttons = new QDialogButtonBox( QDialogButtonBox::Ok|QDialogButtonBox::Cancel );
    connect( buttons, SIGNAL(rejected()), this, SLOT(deleteLater()));
    connect( buttons, SIGNAL(accepted()), this, SLOT(onSetLocation()));
    layout->addWidget( buttons );
}
