/*
 * This file Copyright (C) 2009-2014 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 * $Id$
 */

#include <cassert>
#include <climits> /* INT_MAX */
#include <ctime>

#include <QDateTime>
#include <QDesktopServices>
#include <QEvent>
#include <QFont>
#include <QFontMetrics>
#include <QHeaderView>
#include <QHostAddress>
#include <QInputDialog>
#include <QItemSelectionModel>
#include <QLabel>
#include <QList>
#include <QMap>
#include <QMessageBox>
#include <QResizeEvent>
#include <QStringList>
#include <QStyle>
#include <QTreeWidgetItem>

#include <libtransmission/transmission.h>
#include <libtransmission/utils.h> // tr_getRatio ()

#include "column-resizer.h"
#include "details.h"
#include "file-tree.h"
#include "formatter.h"
#include "hig.h"
#include "prefs.h"
#include "session.h"
#include "squeezelabel.h"
#include "torrent.h"
#include "torrent-model.h"
#include "tracker-delegate.h"
#include "tracker-model.h"
#include "tracker-model-filter.h"
#include "utils.h"

class Prefs;
class Session;

/****
*****
****/

namespace
{
  const int REFRESH_INTERVAL_MSEC = 4000;

  const char * PREF_KEY ("pref-key");

  enum // peer columns
  {
    COL_LOCK,
    COL_UP,
    COL_DOWN,
    COL_PERCENT,
    COL_STATUS,
    COL_ADDRESS,
    COL_CLIENT,
    N_COLUMNS
  };

  int
  measureViewItem (QAbstractItemView * view, const QString& text)
  {
    QStyleOptionViewItemV4 option;
    option.features = QStyleOptionViewItemV2::HasDisplay;
    option.text = text;
    return view->style ()->sizeFromContents (QStyle::CT_ItemViewItem, &option,
      QSize (QWIDGETSIZE_MAX, QWIDGETSIZE_MAX), view).width ();
  }
}

/***
****
***/

class PeerItem: public QTreeWidgetItem
{
    Peer peer;
    mutable QString collatedAddress;
    QString status;

  public:
    PeerItem (const Peer& p): peer(p) {}
    virtual ~PeerItem () {}

  public:
    void refresh (const Peer& p)
    {
      if (p.address != peer.address)
        collatedAddress.clear ();
      peer = p;
    }

    void setStatus (const QString& s) { status = s; }

    virtual bool operator< (const QTreeWidgetItem & other) const
    {
      const PeerItem * i = dynamic_cast<const PeerItem*> (&other);
      QTreeWidget * tw (treeWidget ());
      const int column = tw ? tw->sortColumn () : 0;
      switch (column)
        {
          case COL_UP: return peer.rateToPeer < i->peer.rateToPeer;
          case COL_DOWN: return peer.rateToClient < i->peer.rateToClient;
          case COL_PERCENT: return peer.progress < i->peer.progress;
          case COL_STATUS: return status < i->status;
          case COL_CLIENT: return peer.clientName < i->peer.clientName;
          case COL_LOCK: return peer.isEncrypted && !i->peer.isEncrypted;
          default: return address () < i->address ();
        }
    }

  private:
    const QString& address () const
    {
      if (collatedAddress.isEmpty ())
        {
          QHostAddress ipAddress;
          if (ipAddress.setAddress (peer.address))
            {
              if (ipAddress.protocol () == QAbstractSocket::IPv4Protocol)
                {
                  const quint32 ipv4Address = ipAddress.toIPv4Address ();
                  collatedAddress = QLatin1String ("1-") +
                    QString::fromLatin1 (QByteArray::number (ipv4Address, 16).rightJustified (8, '0'));
                }
              else if (ipAddress.protocol () == QAbstractSocket::IPv6Protocol)
                {
                  const Q_IPV6ADDR ipv6Address = ipAddress.toIPv6Address ();
                  QByteArray tmp (16, '\0');
                  for (int i = 0; i < 16; ++i)
                    tmp[i] = ipv6Address[i];
                  collatedAddress = QLatin1String ("2-") + QString::fromLatin1 (tmp.toHex ());
                }
            }

          if (collatedAddress.isEmpty ())
            collatedAddress = QLatin1String ("3-") + peer.address.toLower ();
        }

      return collatedAddress;
    }
};

/***
****
***/

QIcon
Details::getStockIcon (const QString& freedesktop_name, int fallback)
{
  QIcon icon = QIcon::fromTheme (freedesktop_name);

  if (icon.isNull ())
    icon = style ()->standardIcon (QStyle::StandardPixmap (fallback), 0, this);

  return icon;
}

Details::Details (Session       & session,
                  Prefs         & prefs,
                  TorrentModel  & model,
                  QWidget       * parent):
  QDialog (parent, Qt::Dialog),
  mySession (session),
  myPrefs (prefs),
  myModel (model),
  myChangedTorrents (false),
  myHavePendingRefresh (false)
{
  ui.setupUi(this);

  initInfoTab ();
  initPeersTab ();
  initTrackerTab ();
  initFilesTab ();
  initOptionsTab ();

  adjustSize ();
  ui.commentBrowser->setMaximumHeight (QWIDGETSIZE_MAX);

  setAttribute (Qt::WA_DeleteOnClose, true);

  QList<int> initKeys;
  initKeys << Prefs::SHOW_TRACKER_SCRAPES
           << Prefs::SHOW_BACKUP_TRACKERS;
  foreach (int key, initKeys)
    refreshPref (key);

  connect (&myTimer, SIGNAL (timeout ()), this, SLOT (onTimer ()));
  connect (&myPrefs, SIGNAL (changed (int)), this, SLOT (refreshPref (int)));

  onTimer ();
  myTimer.setSingleShot (false);
  myTimer.start (REFRESH_INTERVAL_MSEC);
}

Details::~Details ()
{
  myTrackerDelegate->deleteLater ();
  myTrackerFilter->deleteLater ();
  myTrackerModel->deleteLater ();
}

void
Details::setIds (const QSet<int>& ids)
{
  if (ids == myIds)
    return;

  myChangedTorrents = true;

  // stop listening to the old torrents
  foreach (int id, myIds)
    {
      const Torrent * tor = myModel.getTorrentFromId (id);
      if (tor)
        disconnect (tor, SIGNAL (torrentChanged (int)), this, SLOT (onTorrentChanged ()));
    }

  ui.filesView->clear ();
  myIds = ids;
  myTrackerModel->refresh (myModel, myIds);

  // listen to the new torrents
  foreach (int id, myIds)
    {
      const Torrent * tor = myModel.getTorrentFromId (id);
      if (tor)
        connect (tor, SIGNAL (torrentChanged (int)), this, SLOT (onTorrentChanged ()));
    }

  for (int i = 0; i < ui.tabs->count (); ++i)
    ui.tabs->widget (i)->setEnabled (false);

  onTimer ();
}

void
Details::refreshPref (int key)
{
  QString str;

  switch (key)
    {
      case Prefs::SHOW_TRACKER_SCRAPES:
        {
          QItemSelectionModel * selectionModel (ui.trackersView->selectionModel ());
          const QItemSelection selection (selectionModel->selection ());
          const QModelIndex currentIndex (selectionModel->currentIndex ());
          myTrackerDelegate->setShowMore (myPrefs.getBool (key));
          selectionModel->clear ();
          ui.trackersView->reset ();
          selectionModel->select (selection, QItemSelectionModel::Select);
          selectionModel->setCurrentIndex (currentIndex, QItemSelectionModel::NoUpdate);
          break;
        }

      case Prefs::SHOW_BACKUP_TRACKERS:
        myTrackerFilter->setShowBackupTrackers (myPrefs.getBool (key));
        break;

      default:
        break;
    }
}


/***
****
***/

QString
Details::timeToStringRounded (int seconds)
{
  if (seconds > 60)
    seconds -= (seconds % 60);

  return Formatter::timeToString (seconds);
}

void
Details::onTimer ()
{
  getNewData ();
}

void
Details::getNewData ()
{
  if (!myIds.empty ())
    {
      QSet<int> infos;
      foreach (int id, myIds)
        {
          const Torrent * tor = myModel.getTorrentFromId (id);
          if (tor->isMagnet ())
            infos.insert (tor->id ());
        }

      if (!infos.isEmpty ())
        mySession.initTorrents (infos);
      mySession.refreshExtraStats (myIds);
    }
}

void
Details::onTorrentChanged ()
{
  if (!myHavePendingRefresh)
    {
      myHavePendingRefresh = true;
      QTimer::singleShot (100, this, SLOT (refresh ()));
    }
}

namespace
{
  void setIfIdle (QComboBox * box, int i)
  {
    if (!box->hasFocus ())
      {
        box->blockSignals (true);
        box->setCurrentIndex (i);
        box->blockSignals (false);
      }
  }

  void setIfIdle (QDoubleSpinBox * spin, double value)
  {
    if (!spin->hasFocus ())
      {
        spin->blockSignals (true);
        spin->setValue (value);
        spin->blockSignals (false);
      }
  }

  void setIfIdle (QSpinBox * spin, int value)
  {
    if (!spin->hasFocus ())
      {
        spin->blockSignals (true);
        spin->setValue (value);
        spin->blockSignals (false);
      }
  }
}

void
Details::refresh ()
{
  const int n = myIds.size ();
  const bool single = n == 1;
  const QString blank;
  const QFontMetrics fm (fontMetrics ());
  QList<const Torrent*> torrents;
  QString string;
  const QString none = tr ("None");
  const QString mixed = tr ("Mixed");
  const QString unknown = tr ("Unknown");

  // build a list of torrents
  foreach (int id, myIds)
    {
      const Torrent * tor = myModel.getTorrentFromId (id);
      if (tor)
        torrents << tor;
    }

  ///
  ///  activity tab
  ///

  // myStateLabel
  if (torrents.empty ())
    {
      string = none;
    }
  else
    {
      bool isMixed = false;
      bool allPaused = true;
      bool allFinished = true;
      const tr_torrent_activity baseline = torrents[0]->getActivity ();
      foreach (const Torrent * t, torrents)
        {
          const tr_torrent_activity activity = t->getActivity ();
          if (activity != baseline)
            isMixed = true;
          if (activity != TR_STATUS_STOPPED)
            allPaused = allFinished = false;
          if (!t->isFinished ())
            allFinished = false;
        }

      if (isMixed)
        string = mixed;
      else if (allFinished)
        string = tr ("Finished");
      else if (allPaused)
        string = tr ("Paused");
      else
        string = torrents[0]->activityString ();
    }
  ui.stateValueLabel->setText (string);
  const QString stateString = string;

  // myHaveLabel
  double sizeWhenDone = 0;
  double available = 0;
  if (torrents.empty ())
    {
      string = none;
    }
  else
    {
      double leftUntilDone = 0;
      int64_t haveTotal = 0;
      int64_t haveVerified = 0;
      int64_t haveUnverified = 0;
      int64_t verifiedPieces = 0;

      foreach (const Torrent * t, torrents)
        {
          if (t->hasMetadata ())
            {
              haveTotal += t->haveTotal ();
              haveUnverified += t->haveUnverified ();
              const uint64_t v = t->haveVerified ();
              haveVerified += v;
              if (t->pieceSize ())
                verifiedPieces += v / t->pieceSize ();
              sizeWhenDone += t->sizeWhenDone ();
              leftUntilDone += t->leftUntilDone ();
              available += t->sizeWhenDone () - t->leftUntilDone () + t->desiredAvailable ();
            }
        }

      const double d = 100.0 * (sizeWhenDone ? (sizeWhenDone - leftUntilDone) / sizeWhenDone : 1);
      QString pct = Formatter::percentToString (d);

      if (!haveUnverified && !leftUntilDone)
        {
          //: Text following the "Have:" label in torrent properties dialog;
          //: %1 is amount of downloaded and verified data
          string = tr ("%1 (100%)")
                     .arg (Formatter::sizeToString (haveVerified));
        }
      else if (!haveUnverified)
        {
          //: Text following the "Have:" label in torrent properties dialog;
          //: %1 is amount of downloaded and verified data,
          //: %2 is overall size of torrent data,
          //: %3 is percentage (%1/%2*100)
          string = tr ("%1 of %2 (%3%)")
                     .arg (Formatter::sizeToString (haveVerified))
                     .arg (Formatter::sizeToString (sizeWhenDone))
                     .arg (pct);
        }
      else
        {
          //: Text following the "Have:" label in torrent properties dialog;
          //: %1 is amount of downloaded data (both verified and unverified),
          //: %2 is overall size of torrent data,
          //: %3 is percentage (%1/%2*100),
          //: %4 is amount of downloaded but not yet verified data
          string = tr ("%1 of %2 (%3%), %4 Unverified")
                     .arg (Formatter::sizeToString (haveVerified + haveUnverified))
                     .arg (Formatter::sizeToString (sizeWhenDone))
                     .arg (pct)
                     .arg (Formatter::sizeToString (haveUnverified));
        }
    }
  ui.haveValueLabel->setText (string);

  // myAvailabilityLabel
  if (torrents.empty ())
    string = none;
  else if (sizeWhenDone == 0)
    string = none;
  else
    string = QString::fromLatin1 ("%1%").arg (Formatter::percentToString ( (100.0 * available) / sizeWhenDone));
  ui.availabilityValueLabel->setText (string);

  // myDownloadedLabel
  if (torrents.empty ())
    {
      string = none;
    }
  else
    {
      uint64_t d = 0;
      uint64_t f = 0;
      foreach (const Torrent * t, torrents)
        {
          d += t->downloadedEver ();
          f += t->failedEver ();
        }
      const QString dstr = Formatter::sizeToString (d);
      const QString fstr = Formatter::sizeToString (f);
      if (f)
        string = tr ("%1 (%2 corrupt)").arg (dstr).arg (fstr);
      else
        string = dstr;
    }
  ui.downloadedValueLabel->setText (string);

  //  myUploadedLabel
  if (torrents.empty ())
    {
      string = none;
    }
  else
    {
      uint64_t u = 0;
      uint64_t d = 0;
      foreach (const Torrent * t, torrents)
        {
          u += t->uploadedEver ();
          d += t->downloadedEver ();
        }
      string = tr ("%1 (Ratio: %2)")
                 .arg (Formatter::sizeToString (u))
                 .arg (Formatter::ratioToString (tr_getRatio (u, d)));
    }
  ui.uploadedValueLabel->setText (string);

  const QDateTime qdt_now = QDateTime::currentDateTime ();

  // myRunTimeLabel
  if (torrents.empty ())
    {
      string = none;
    }
  else
    {
      bool allPaused = true;
      QDateTime baseline = torrents[0]->lastStarted ();
      foreach (const Torrent * t, torrents)
        {
          if (baseline != t->lastStarted ())
            baseline = QDateTime ();
          if (!t->isPaused ())
            allPaused = false;
        }

      if (allPaused)
        string = stateString; // paused || finished
      else if (baseline.isNull ())
        string = mixed;
      else
        string = Formatter::timeToString (baseline.secsTo (qdt_now));
    }
  ui.runningTimeValueLabel->setText (string);


  // myETALabel
  string.clear ();
  if (torrents.empty ())
    {
      string = none;
    }
  else
    {
      int baseline = torrents[0]->getETA ();
      foreach (const Torrent * t, torrents)
        {
          if (baseline != t->getETA ())
            {
              string = mixed;
              break;
            }
        }

      if (string.isEmpty ())
        {
          if (baseline < 0)
            string = tr ("Unknown");
          else
            string = Formatter::timeToString (baseline);
       }
    }
  ui.remainingTimeValueLabel->setText (string);


  // myLastActivityLabel
  if (torrents.empty ())
    {
      string = none;
    }
  else
    {
      QDateTime latest = torrents[0]->lastActivity ();
      foreach (const Torrent * t, torrents)
        {
          const QDateTime dt = t->lastActivity ();
          if (latest < dt)
            latest = dt;
        }

      const int seconds = latest.isValid () ? latest.secsTo (qdt_now) : -1;
      if (seconds < 0)
        string = none;
      else if (seconds < 5)
        string = tr ("Active now");
      else
        string = tr ("%1 ago").arg (Formatter::timeToString (seconds));
    }
  ui.lastActivityValueLabel->setText (string);


  if (torrents.empty ())
    {
      string = none;
    }
  else
    {
      string = torrents[0]->getError ();
      foreach (const Torrent * t, torrents)
        {
          if (string != t->getError ())
            {
              string = mixed;
              break;
            }
        }
    }
  if (string.isEmpty ())
    string = none;
  ui.errorValueLabel->setText (string);


  ///
  /// information tab
  ///

  // mySizeLabel
  if (torrents.empty ())
    {
      string = none;
    }
  else
    {
      int pieces = 0;
      uint64_t size = 0;
      uint32_t pieceSize = torrents[0]->pieceSize ();
      foreach (const Torrent * t, torrents)
        {
          pieces += t->pieceCount ();
          size += t->totalSize ();
          if (pieceSize != t->pieceSize ())
            pieceSize = 0;
        }

      if (!size)
        string = none;
      else if (pieceSize > 0)
        string = tr ("%1 (%Ln pieces @ %2)", "", pieces)
                   .arg (Formatter::sizeToString (size))
                   .arg (Formatter::memToString (pieceSize));
      else
        string = tr ("%1 (%Ln pieces)", "", pieces)
                   .arg (Formatter::sizeToString (size));
    }
  ui.sizeValueLabel->setText (string);

  // myHashLabel
  string = none;
  if (!torrents.empty ())
    {
      string = torrents[0]->hashString ();
      foreach (const Torrent * t, torrents)
        {
          if (string != t->hashString ())
            {
              string = mixed;
              break;
            }
        }
    }
  ui.hashValueLabel->setText (string);

  // myPrivacyLabel
  string = none;
  if (!torrents.empty ())
    {
      bool b = torrents[0]->isPrivate ();
      string = b ? tr ("Private to this tracker -- DHT and PEX disabled")
                 : tr ("Public torrent");
      foreach (const Torrent * t, torrents)
        {
          if (b != t->isPrivate ())
            {
              string = mixed;
              break;
            }
        }
    }
  ui.privacyValueLabel->setText (string);

  // myCommentBrowser
  string = none;
  bool isCommentMixed = false;
  if (!torrents.empty ())
    {
      string = torrents[0]->comment ();
      foreach (const Torrent * t, torrents)
        {
          if (string != t->comment ())
            {
              string = mixed;
              isCommentMixed = true;
              break;
            }
        }
    }
  if (ui.commentBrowser->toPlainText() != string)
    {
      ui.commentBrowser->setText (string);
    }
  ui.commentBrowser->setEnabled (!isCommentMixed && !string.isEmpty ());

  // myOriginLabel
  string = none;
  if (!torrents.empty ())
    {
      bool mixed_creator=false, mixed_date=false;
      const QString creator = torrents[0]->creator ();
      const QString date = torrents[0]->dateCreated ().toString ();
      foreach (const Torrent * t, torrents)
        {
          mixed_creator |= (creator != t->creator ());
          mixed_date |= (date != t->dateCreated ().toString ());
        }

      if (mixed_creator && mixed_date)
        string = mixed;
      else if (mixed_date && !creator.isEmpty ())
        string = tr ("Created by %1").arg (creator);
      else if (mixed_creator && !date.isEmpty ())
        string = tr ("Created on %1").arg (date);
      else if (creator.isEmpty () && date.isEmpty ())
        string = tr ("N/A");
      else
        string = tr ("Created by %1 on %2").arg (creator).arg (date);
    }
  ui.originValueLabel->setText (string);

  // myLocationLabel
  string = none;
  if (!torrents.empty ())
    {
      string = torrents[0]->getPath ();
      foreach (const Torrent * t, torrents)
        {
          if (string != t->getPath ())
            {
              string = mixed;
              break;
            }
        }
    }
  ui.locationValueLabel->setText (string);


  ///
  ///  Options Tab
  ///

  if (myChangedTorrents && !torrents.empty ())
    {
      int i;
      bool uniform;
      bool baselineFlag;
      int baselineInt;
      const Torrent * tor;
      const Torrent * baseline = *torrents.begin ();

      // mySessionLimitCheck
      uniform = true;
      baselineFlag = baseline->honorsSessionLimits ();
      foreach (tor, torrents) if (baselineFlag != tor->honorsSessionLimits ()) { uniform = false; break; }
      ui.sessionLimitCheck->setChecked (uniform && baselineFlag);

      // mySingleDownCheck
      uniform = true;
      baselineFlag = baseline->downloadIsLimited ();
      foreach (tor, torrents) if (baselineFlag != tor->downloadIsLimited ()) { uniform = false; break; }
      ui.singleDownCheck->setChecked (uniform && baselineFlag);

      // mySingleUpCheck
      uniform = true;
      baselineFlag = baseline->uploadIsLimited ();
      foreach (tor, torrents) if (baselineFlag != tor->uploadIsLimited ()) { uniform = false; break; }
      ui.singleUpCheck->setChecked (uniform && baselineFlag);

      // myBandwidthPriorityCombo
      uniform = true;
      baselineInt = baseline->getBandwidthPriority ();
      foreach (tor, torrents) if (baselineInt != tor->getBandwidthPriority ()) { uniform = false; break; }
      if (uniform)
        i = ui.bandwidthPriorityCombo->findData (baselineInt);
      else
        i = -1;
      setIfIdle (ui.bandwidthPriorityCombo, i);

      setIfIdle (ui.singleDownSpin, int (tor->downloadLimit ().KBps ()));
      setIfIdle (ui.singleUpSpin, int (tor->uploadLimit ().KBps ()));
      setIfIdle (ui.peerLimitSpin, tor->peerLimit ());
    }

  if (!torrents.empty ())
    {
      const Torrent * tor;

      // ratio
      bool uniform = true;
      int baselineInt = torrents[0]->seedRatioMode ();
      foreach (tor, torrents) if (baselineInt != tor->seedRatioMode ()) { uniform = false; break; }

      setIfIdle (ui.ratioCombo, uniform ? ui.ratioCombo->findData (baselineInt) : -1);
      ui.ratioSpin->setVisible (uniform && (baselineInt == TR_RATIOLIMIT_SINGLE));

      setIfIdle (ui.ratioSpin, tor->seedRatioLimit ());

      // idle
      uniform = true;
      baselineInt = torrents[0]->seedIdleMode ();
      foreach (tor, torrents) if (baselineInt != tor->seedIdleMode ()) { uniform = false; break; }

      setIfIdle (ui.idleCombo, uniform ? ui.idleCombo->findData (baselineInt) : -1);
      ui.idleSpin->setVisible (uniform && (baselineInt == TR_RATIOLIMIT_SINGLE));

      setIfIdle (ui.idleSpin, tor->seedIdleLimit ());
      onIdleLimitChanged ();
    }

  ///
  ///  Tracker tab
  ///

  myTrackerModel->refresh (myModel, myIds);

  ///
  ///  Peers tab
  ///

  QMap<QString,QTreeWidgetItem*> peers2;
  QList<QTreeWidgetItem*> newItems;
  foreach (const Torrent * t, torrents)
    {
      const QString idStr (QString::number (t->id ()));
      PeerList peers = t->peers ();

      foreach (const Peer& peer, peers)
        {
          const QString key = idStr + ":" + peer.address;
          PeerItem * item = static_cast<PeerItem*> (myPeers.value (key, 0));

          if (item == 0) // new peer has connected
            {
              static const QIcon myEncryptionIcon (":/icons/encrypted.png");
              static const QIcon myEmptyIcon;
              item = new PeerItem (peer);
              item->setTextAlignment (COL_UP, Qt::AlignRight|Qt::AlignVCenter);
              item->setTextAlignment (COL_DOWN, Qt::AlignRight|Qt::AlignVCenter);
              item->setTextAlignment (COL_PERCENT, Qt::AlignRight|Qt::AlignVCenter);
              item->setIcon (COL_LOCK, peer.isEncrypted ? myEncryptionIcon : myEmptyIcon);
              item->setToolTip (COL_LOCK, peer.isEncrypted ? tr ("Encrypted connection") : "");
              item->setText (COL_ADDRESS, peer.address);
              item->setText (COL_CLIENT, peer.clientName);
              newItems << item;
            }

          const QString code = peer.flagStr;
          item->setStatus (code);
          item->refresh (peer);

          QString codeTip;
          foreach (QChar ch, code)
            {
              QString txt;
              switch (ch.unicode ())
                {
                  case 'O': txt = tr ("Optimistic unchoke"); break;
                  case 'D': txt = tr ("Downloading from this peer"); break;
                  case 'd': txt = tr ("We would download from this peer if they would let us"); break;
                  case 'U': txt = tr ("Uploading to peer"); break;
                  case 'u': txt = tr ("We would upload to this peer if they asked"); break;
                  case 'K': txt = tr ("Peer has unchoked us, but we're not interested"); break;
                  case '?': txt = tr ("We unchoked this peer, but they're not interested"); break;
                  case 'E': txt = tr ("Encrypted connection"); break;
                  case 'H': txt = tr ("Peer was discovered through DHT"); break;
                  case 'X': txt = tr ("Peer was discovered through Peer Exchange (PEX)"); break;
                  case 'I': txt = tr ("Peer is an incoming connection"); break;
                  case 'T': txt = tr ("Peer is connected over uTP"); break;
                }

              if (!txt.isEmpty ())
                codeTip += QString::fromLatin1 ("%1: %2\n").arg (ch).arg (txt);
            }

          if (!codeTip.isEmpty ())
            codeTip.resize (codeTip.size ()-1); // eat the trailing linefeed

          item->setText (COL_UP, peer.rateToPeer.isZero () ? "" : Formatter::speedToString (peer.rateToPeer));
          item->setText (COL_DOWN, peer.rateToClient.isZero () ? "" : Formatter::speedToString (peer.rateToClient));
          item->setText (COL_PERCENT, peer.progress > 0 ? QString::fromLatin1 ("%1%").arg ( (int) (peer.progress * 100.0)) : "");
          item->setText (COL_STATUS, code);
          item->setToolTip (COL_STATUS, codeTip);

          peers2.insert (key, item);
        }
    }

  ui.peersView->addTopLevelItems (newItems);
  foreach (QString key, myPeers.keys ())
    {
      if (!peers2.contains (key)) // old peer has disconnected
        {
          QTreeWidgetItem * item = myPeers.value (key, 0);
          ui.peersView->takeTopLevelItem (ui.peersView->indexOfTopLevelItem (item));
          delete item;
        }
    }
  myPeers = peers2;

  if (!single)
    ui.filesView->clear ();
  if (single)
    ui.filesView->update (torrents[0]->files (), myChangedTorrents);

  myChangedTorrents = false;
  myHavePendingRefresh = false;
  for (int i = 0; i < ui.tabs->count (); ++i)
    ui.tabs->widget (i)->setEnabled (true);
}

/***
****
***/

void
Details::initInfoTab ()
{
  const int h = QFontMetrics (ui.commentBrowser->font ()).lineSpacing () * 4;
  ui.commentBrowser->setFixedHeight (h);

  ColumnResizer * cr (new ColumnResizer (this));
  cr->addLayout (ui.activitySectionLayout);
  cr->addLayout (ui.detailsSectionLayout);
  cr->update ();
}

/***
****
***/

void
Details::onShowTrackerScrapesToggled (bool val)
{
  myPrefs.set (Prefs::SHOW_TRACKER_SCRAPES, val);
}

void
Details::onShowBackupTrackersToggled (bool val)
{
  myPrefs.set (Prefs::SHOW_BACKUP_TRACKERS, val);
}

void
Details::onHonorsSessionLimitsToggled (bool val)
{
  mySession.torrentSet (myIds, TR_KEY_honorsSessionLimits, val);
  getNewData ();
}
void
Details::onDownloadLimitedToggled (bool val)
{
  mySession.torrentSet (myIds, TR_KEY_downloadLimited, val);
  getNewData ();
}
void
Details::onSpinBoxEditingFinished ()
{
  const QObject * spin = sender ();
  const tr_quark key = spin->property (PREF_KEY).toInt ();
  const QDoubleSpinBox * d = qobject_cast<const QDoubleSpinBox*> (spin);
  if (d)
    mySession.torrentSet (myIds, key, d->value ());
  else
    mySession.torrentSet (myIds, key, qobject_cast<const QSpinBox*> (spin)->value ());
  getNewData ();
}

void
Details::onUploadLimitedToggled (bool val)
{
  mySession.torrentSet (myIds, TR_KEY_uploadLimited, val);
  getNewData ();
}

void
Details::onIdleModeChanged (int index)
{
  const int val = ui.idleCombo->itemData (index).toInt ();
  mySession.torrentSet (myIds, TR_KEY_seedIdleMode, val);
  getNewData ();
}

void
Details::onIdleLimitChanged ()
{
  //: Spin box suffix, "Stop seeding if idle for: [ 5 minutes ]" (includes leading space after the number, if needed)
  const QString unitsSuffix = tr (" minute(s)", 0, ui.idleSpin->value ());
  if (ui.idleSpin->suffix () != unitsSuffix)
    ui.idleSpin->setSuffix (unitsSuffix);
}

void
Details::onRatioModeChanged (int index)
{
  const int val = ui.ratioCombo->itemData (index).toInt ();
  mySession.torrentSet (myIds, TR_KEY_seedRatioMode, val);
}

void
Details::onBandwidthPriorityChanged (int index)
{
  if (index != -1)
    {
      const int priority = ui.bandwidthPriorityCombo->itemData (index).toInt ();
      mySession.torrentSet (myIds, TR_KEY_bandwidthPriority, priority);
      getNewData ();
    }
}

void
Details::onTrackerSelectionChanged ()
{
  const int selectionCount = ui.trackersView->selectionModel ()->selectedRows ().size ();
  ui.editTrackerButton->setEnabled (selectionCount == 1);
  ui.removeTrackerButton->setEnabled (selectionCount > 0);
}

void
Details::onAddTrackerClicked ()
{
  bool ok = false;
  const QString url = QInputDialog::getText (this,
                                             tr ("Add URL "),
                                             tr ("Add tracker announce URL:"),
                                             QLineEdit::Normal, QString (), &ok);
  if (!ok)
    {
      // user pressed "cancel" -- noop
    }
  else if (!QUrl (url).isValid ())
    {
      QMessageBox::warning (this, tr ("Error"), tr ("Invalid URL \"%1\"").arg (url));
    }
  else
    {
      QSet<int> ids;

      foreach (int id, myIds)
        if (myTrackerModel->find (id,url) == -1)
          ids.insert (id);

      if (ids.empty ()) // all the torrents already have this tracker
        {
          QMessageBox::warning (this, tr ("Error"), tr ("Tracker already exists."));
        }
        else
        {
          QStringList urls;
          urls << url;
          mySession.torrentSet (ids, TR_KEY_trackerAdd, urls);
          getNewData ();
        }
    }
}

void
Details::onEditTrackerClicked ()
{
  QItemSelectionModel * selectionModel = ui.trackersView->selectionModel ();
  QModelIndexList selectedRows = selectionModel->selectedRows ();
  assert (selectedRows.size () == 1);
  QModelIndex i = selectionModel->currentIndex ();
  const TrackerInfo trackerInfo = ui.trackersView->model ()->data (i, TrackerModel::TrackerRole).value<TrackerInfo> ();

  bool ok = false;
  const QString newval = QInputDialog::getText (this,
                                                tr ("Edit URL "),
                                                tr ("Edit tracker announce URL:"),
                                                QLineEdit::Normal,
                                                trackerInfo.st.announce, &ok);

  if (!ok)
    {
      // user pressed "cancel" -- noop
    }
  else if (!QUrl (newval).isValid ())
    {
      QMessageBox::warning (this, tr ("Error"), tr ("Invalid URL \"%1\"").arg (newval));
    }
    else
    {
      QSet<int> ids;
      ids << trackerInfo.torrentId;

      const QPair<int,QString> idUrl = qMakePair (trackerInfo.st.id, newval);

      mySession.torrentSet (ids, TR_KEY_trackerReplace, idUrl);
      getNewData ();
    }
}

void
Details::onRemoveTrackerClicked ()
{
  // make a map of torrentIds to announce URLs to remove
  QItemSelectionModel * selectionModel = ui.trackersView->selectionModel ();
  QModelIndexList selectedRows = selectionModel->selectedRows ();
  QMap<int,int> torrentId_to_trackerIds;
  foreach (QModelIndex i, selectedRows)
    {
      const TrackerInfo inf = ui.trackersView->model ()->data (i, TrackerModel::TrackerRole).value<TrackerInfo> ();
      torrentId_to_trackerIds.insertMulti (inf.torrentId, inf.st.id);
    }

  // batch all of a tracker's torrents into one command
  foreach (int id, torrentId_to_trackerIds.uniqueKeys ())
    {
      QSet<int> ids;
      ids << id;
      mySession.torrentSet (ids, TR_KEY_trackerRemove, torrentId_to_trackerIds.values (id));
    }

  selectionModel->clearSelection ();
  getNewData ();
}

void
Details::initOptionsTab ()
{
  const QString speed_K_str = Formatter::unitStr (Formatter::SPEED, Formatter::KB);

  ui.singleDownSpin->setSuffix (QString::fromLatin1 (" %1").arg (speed_K_str));
  ui.singleUpSpin->setSuffix (QString::fromLatin1 (" %1").arg (speed_K_str));

  ui.singleDownSpin->setProperty (PREF_KEY, TR_KEY_downloadLimit);
  ui.singleUpSpin->setProperty (PREF_KEY, TR_KEY_uploadLimit);
  ui.ratioSpin->setProperty (PREF_KEY, TR_KEY_seedRatioLimit);
  ui.idleSpin->setProperty (PREF_KEY, TR_KEY_seedIdleLimit);
  ui.peerLimitSpin->setProperty (PREF_KEY, TR_KEY_peer_limit);

  ui.bandwidthPriorityCombo->addItem (tr ("High"),   TR_PRI_HIGH);
  ui.bandwidthPriorityCombo->addItem (tr ("Normal"), TR_PRI_NORMAL);
  ui.bandwidthPriorityCombo->addItem (tr ("Low"),    TR_PRI_LOW);

  ui.ratioCombo->addItem (tr ("Use Global Settings"),      TR_RATIOLIMIT_GLOBAL);
  ui.ratioCombo->addItem (tr ("Seed regardless of ratio"), TR_RATIOLIMIT_UNLIMITED);
  ui.ratioCombo->addItem (tr ("Stop seeding at ratio:"),   TR_RATIOLIMIT_SINGLE);

  ui.idleCombo->addItem (tr ("Use Global Settings"),         TR_IDLELIMIT_GLOBAL);
  ui.idleCombo->addItem (tr ("Seed regardless of activity"), TR_IDLELIMIT_UNLIMITED);
  ui.idleCombo->addItem (tr ("Stop seeding if idle for:"),   TR_IDLELIMIT_SINGLE);

  ColumnResizer * cr (new ColumnResizer (this));
  cr->addLayout (ui.speedSectionLayout);
  cr->addLayout (ui.seedingLimitsSectionRatioLayout);
  cr->addLayout (ui.seedingLimitsSectionIdleLayout);
  cr->addLayout (ui.peerConnectionsSectionLayout);
  cr->update ();

  connect (ui.sessionLimitCheck, SIGNAL (clicked (bool)), SLOT (onHonorsSessionLimitsToggled (bool)));
  connect (ui.singleDownCheck, SIGNAL (clicked (bool)), SLOT (onDownloadLimitedToggled (bool)));
  connect (ui.singleDownSpin, SIGNAL (editingFinished ()), SLOT (onSpinBoxEditingFinished ()));
  connect (ui.singleUpCheck, SIGNAL (clicked (bool)), SLOT (onUploadLimitedToggled (bool)));
  connect (ui.singleUpSpin, SIGNAL (editingFinished ()), SLOT (onSpinBoxEditingFinished ()));
  connect (ui.bandwidthPriorityCombo, SIGNAL (currentIndexChanged (int)), SLOT (onBandwidthPriorityChanged (int)));
  connect (ui.ratioCombo, SIGNAL (currentIndexChanged (int)), SLOT (onRatioModeChanged (int)));
  connect (ui.ratioSpin, SIGNAL (editingFinished ()), SLOT (onSpinBoxEditingFinished ()));
  connect (ui.idleCombo, SIGNAL (currentIndexChanged (int)), SLOT (onIdleModeChanged (int)));
  connect (ui.idleSpin, SIGNAL (editingFinished ()), SLOT (onSpinBoxEditingFinished ()));
  connect (ui.idleSpin, SIGNAL (valueChanged (int)), SLOT (onIdleLimitChanged ()));
  connect (ui.peerLimitSpin, SIGNAL (editingFinished ()), SLOT (onSpinBoxEditingFinished ()));
}

/***
****
***/

void
Details::initTrackerTab ()
{
  myTrackerModel = new TrackerModel ();
  myTrackerFilter = new TrackerModelFilter ();
  myTrackerFilter->setSourceModel (myTrackerModel);
  myTrackerDelegate = new TrackerDelegate ();

  ui.trackersView->setModel (myTrackerFilter);
  ui.trackersView->setItemDelegate (myTrackerDelegate);

  ui.addTrackerButton->setIcon (getStockIcon ("list-add", QStyle::SP_DialogOpenButton));
  ui.editTrackerButton->setIcon (getStockIcon ("document-properties", QStyle::SP_DesktopIcon));
  ui.removeTrackerButton->setIcon (getStockIcon ("list-remove", QStyle::SP_TrashIcon));

  ui.showTrackerScrapesCheck->setChecked (myPrefs.getBool (Prefs::SHOW_TRACKER_SCRAPES));
  ui.showBackupTrackersCheck->setChecked (myPrefs.getBool (Prefs::SHOW_BACKUP_TRACKERS));

  connect (ui.trackersView->selectionModel (), SIGNAL (selectionChanged (QItemSelection, QItemSelection)),
    SLOT (onTrackerSelectionChanged ()));
  connect (ui.addTrackerButton, SIGNAL (clicked ()), SLOT (onAddTrackerClicked ()));
  connect (ui.editTrackerButton, SIGNAL (clicked ()), SLOT (onEditTrackerClicked ()));
  connect (ui.removeTrackerButton, SIGNAL (clicked ()), SLOT (onRemoveTrackerClicked ()));
  connect (ui.showTrackerScrapesCheck, SIGNAL (clicked (bool)), SLOT (onShowTrackerScrapesToggled (bool)));
  connect (ui.showBackupTrackersCheck, SIGNAL (clicked (bool)), SLOT (onShowBackupTrackersToggled (bool)));

  onTrackerSelectionChanged ();
}

/***
****
***/

void
Details::initPeersTab ()
{
  QStringList headers;
  headers << QString () << tr ("Up") << tr ("Down") << tr ("%") << tr ("Status") << tr ("Address") << tr ("Client");

  ui.peersView->setHeaderLabels (headers);
  ui.peersView->sortByColumn (COL_ADDRESS, Qt::AscendingOrder);

  ui.peersView->setColumnWidth (COL_LOCK, 20);
  ui.peersView->setColumnWidth (COL_UP, measureViewItem (ui.peersView, "1024 MiB/s"));
  ui.peersView->setColumnWidth (COL_DOWN, measureViewItem (ui.peersView, "1024 MiB/s"));
  ui.peersView->setColumnWidth (COL_PERCENT, measureViewItem (ui.peersView, "100%"));
  ui.peersView->setColumnWidth (COL_STATUS, measureViewItem (ui.peersView, "ODUK?EXI"));
  ui.peersView->setColumnWidth (COL_ADDRESS, measureViewItem (ui.peersView, "888.888.888.888"));
}

/***
****
***/

void
Details::initFilesTab ()
{
  connect (ui.filesView, SIGNAL (priorityChanged (QSet<int>, int)), SLOT (onFilePriorityChanged (QSet<int>, int)));
  connect (ui.filesView, SIGNAL (wantedChanged (QSet<int>, bool)), SLOT (onFileWantedChanged (QSet<int>, bool)));
  connect (ui.filesView, SIGNAL (pathEdited (QString, QString)), SLOT (onPathEdited (QString, QString)));
  connect (ui.filesView, SIGNAL (openRequested (QString)), SLOT (onOpenRequested (QString)));
}

void
Details::onFilePriorityChanged (const QSet<int>& indices, int priority)
{
  tr_quark key;

  switch (priority)
    {
      case TR_PRI_LOW:
        key = TR_KEY_priority_low;
        break;

      case TR_PRI_HIGH:
        key = TR_KEY_priority_high;
        break;

      default:
        key = TR_KEY_priority_normal;
        break;
    }

  mySession.torrentSet (myIds, key, indices.toList ());
  getNewData ();
}

void
Details::onFileWantedChanged (const QSet<int>& indices, bool wanted)
{
  const tr_quark key = wanted ? TR_KEY_files_wanted : TR_KEY_files_unwanted;
  mySession.torrentSet (myIds, key, indices.toList ());
  getNewData ();
}

void
Details::onPathEdited (const QString& oldpath, const QString& newname)
{
  mySession.torrentRenamePath (myIds, oldpath, newname);
}

void
Details::onOpenRequested (const QString& path)
{
  if (!mySession.isLocal ())
    return;

  foreach (const int id, myIds)
    {
      const Torrent * const tor = myModel.getTorrentFromId (id);
      if (tor == NULL)
        continue;

      const QString localFilePath = tor->getPath () + "/" + path;
      if (!QFile::exists (localFilePath))
        continue;

      if (QDesktopServices::openUrl (QUrl::fromLocalFile (localFilePath)))
        break;
    }
}
