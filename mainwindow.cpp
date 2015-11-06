/*

    Copyright (c) 2015 Oliver Lau <ola@ct.de>, Heise Medien GmbH & Co. KG

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.

*/


#include <QDebug>
#include <QtAlgorithms>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QDateTime>
#include <QDesktopServices>
#include <QJsonDocument>
#include <QDir>
#include <QFile>
#include <QPoint>
#include <QGraphicsOpacityEffect>
#include <QTime>
#include <QVector>
#include <qmath.h>

#include "globals.h"
#include "mainwindow.h"
#include "ui_mainwindow.h"

#include "o1twitter.h"
#include "o1requestor.h"
#include "o2globals.h"
#include "o2settingsstore.h"

struct KineticData {
    KineticData(void) : t(0) { /* ... */ }
    KineticData(const QPoint& p, int t) : p(p), t(t) { /* ... */ }
    QPoint p;
    int t;
};

QDebug operator<<(QDebug debug, const KineticData &kd)
{
  QDebugStateSaver saver(debug);
  (void)saver;
  debug.nospace() << "KineticData(" << kd.p << "," << kd.t << ")";
  return debug;
}


static const int MaxKineticDataSamples = 5;
const qreal Friction = 0.95;
const int TimeInterval = 25;


class MainWindowPrivate
{
public:
  MainWindowPrivate(QObject *parent)
    : oauth(new O1Twitter(parent))
    , store(new O2SettingsStore(O2_ENCRYPTION_KEY))
    , settings(QSettings::IniFormat, QSettings::UserScope, AppCompanyName, AppName)
    , reply(Q_NULLPTR)
    , NAM(parent)
    , mouseDown(false)
    , opacityEffect(Q_NULLPTR)
    , mouseMoveTimerId(0)
      /*
       * From the Qt docs: "QStandardPaths::DataLocation returns the
       * same value as AppLocalDataLocation. This enumeration value
       * is deprecated. Using AppDataLocation is preferable since on
       * Windows, the roaming path is recommended."
       *
       * AppLocalDataLocation was introduced in Qt 5.4. To maintain
       * compatibility to Qt 5.3 (which is found in many reasonably
       * current Linux distributions this code still uses the
       * deprecated value DataLocation.
       *
       */
    , tweetFilepath(QStandardPaths::writableLocation(QStandardPaths::DataLocation))
    , lastId(0)
  {
    store->setGroupKey("twitter");
    oauth->setStore(store);
    oauth->setClientId(MY_CLIENT_KEY);
    oauth->setClientSecret(MY_CLIENT_SECRET);
    oauth->setLocalPort(44333);
    oauth->setSignatureMethod(O2_SIGNATURE_TYPE_HMAC_SHA1);
  }
  ~MainWindowPrivate()
  {
    /* ... */
  }

  QVector<KineticData> kineticData;
  O1Twitter *oauth;
  O2SettingsStore *store;
  QSettings settings;
  QNetworkAccessManager NAM;
  QNetworkReply *reply;
  QString tweetFilepath;
  QString tweetFilename;
  QString badTweetFilename;
  QString goodTweetFilename;
  QJsonDocument storedTweets;
  QJsonDocument badTweets;
  QJsonDocument goodTweets;
  qlonglong lastId;
  QPoint lastWidgetPos;
  QPoint lastMousePos;
  bool mouseDown;
  QGraphicsOpacityEffect *opacityEffect;
  QTime mouseMoveTimer;
  int mouseMoveTimerId;
  QPointF velocity;
};



MainWindow::MainWindow(QWidget *parent)
  : QMainWindow(parent)
  , ui(new Ui::MainWindow)
  , d_ptr(new MainWindowPrivate(this))
{
  Q_D(MainWindow);
  ui->setupUi(this);

  d->tweetFilename = d->tweetFilepath + "/all_tweets_of_" + d->settings.value("twitter/userId").toString() + ".json";
  d->badTweetFilename = d->tweetFilepath + "/bad_tweets_of_" + d->settings.value("twitter/userId").toString() + ".json";
  d->goodTweetFilename = d->tweetFilepath + "/good_tweets_of_" + d->settings.value("twitter/userId").toString() + ".json";

  bool ok;

  ok = QDir().mkpath(d->tweetFilepath);

  QFile tweetFile(d->tweetFilename);
  ok = tweetFile.open(QIODevice::ReadOnly);
  if (ok) {
    d->storedTweets = QJsonDocument::fromJson(tweetFile.readAll());
    tweetFile.close();
  }

  QFile badTweetsFile(d->badTweetFilename);
  ok = badTweetsFile.open(QIODevice::ReadOnly);
  if (ok) {
    d->badTweets = QJsonDocument::fromJson(badTweetsFile.readAll());
    badTweetsFile.close();
  }

  QFile goodTweets(d->goodTweetFilename);
  ok = goodTweets.open(QIODevice::ReadOnly);
  if (ok) {
    d->goodTweets = QJsonDocument::fromJson(goodTweets.readAll());
    goodTweets.close();
  }

  d->lastId = 0;
  if (!d->storedTweets.toVariant().toList().isEmpty())
    d->lastId = qMax(d->storedTweets.toVariant().toList().first().toMap()["id"].toLongLong(), d->lastId);
  if (!d->badTweets.toVariant().toList().isEmpty())
    d->lastId = qMax(d->badTweets.toVariant().toList().first().toMap()["id"].toLongLong(), d->lastId);
  if (!d->goodTweets.toVariant().toList().isEmpty())
    d->lastId = qMax(d->goodTweets.toVariant().toList().first().toMap()["id"].toLongLong(), d->lastId);

  QObject::connect(d->oauth, SIGNAL(linkedChanged()), SLOT(onLinkedChanged()));
  QObject::connect(d->oauth, SIGNAL(linkingFailed()), SLOT(onLinkingFailed()));
  QObject::connect(d->oauth, SIGNAL(linkingSucceeded()), SLOT(onLinkingSucceeded()));
  QObject::connect(d->oauth, SIGNAL(openBrowser(QUrl)), SLOT(onOpenBrowser(QUrl)));
  QObject::connect(d->oauth, SIGNAL(closeBrowser()), SLOT(onCloseBrowser()));
  QObject::connect(ui->actionExit, SIGNAL(triggered(bool)), SLOT(close()));
  QObject::connect(ui->actionRefresh, SIGNAL(triggered(bool)), SLOT(getUserTimeline()));
  ui->tweetLabel->installEventFilter(this);
  d->opacityEffect = new QGraphicsOpacityEffect(ui->tweetLabel);
  d->opacityEffect->setOpacity(1.0);
  ui->tweetLabel->setGraphicsEffect(d->opacityEffect);

  QObject::connect(&d->NAM, SIGNAL(finished(QNetworkReply*)), this, SLOT(gotUserTimeline(QNetworkReply*)));

  ui->tableWidget->verticalHeader()->hide();

  restoreSettings();

  d->oauth->link();
}


MainWindow::~MainWindow()
{
  delete ui;
}


void MainWindow::closeEvent(QCloseEvent*)
{
  stopMotion();
  saveSettings();
}


void MainWindow::timerEvent(QTimerEvent *e)
{
  Q_D(MainWindow);
  if (e->timerId() == d->mouseMoveTimerId) {
    if (d->velocity.manhattanLength() > M_SQRT2) {
      scrollBy(d->velocity.toPoint());
      d->velocity *= Friction;
    }
    else {
      stopMotion();
    }
  }
}


void MainWindow::startMotion(const QPointF& velocity)
{
  Q_D(MainWindow);
  d->velocity = velocity;
  if (d->mouseMoveTimerId == 0)
    d->mouseMoveTimerId = startTimer(TimeInterval);
}


void MainWindow::stopMotion(void)
{
  Q_D(MainWindow);
  if (d->mouseMoveTimerId) {
    killTimer(d->mouseMoveTimerId);
    d->mouseMoveTimerId = 0;
  }
  d->velocity = QPointF();
}


void MainWindow::scrollBy(const QPoint &offset)
{
  Q_D(MainWindow);
  ui->tweetLabel->move(offset.x() + d->lastWidgetPos.x(), d->lastWidgetPos.y());
  d->lastWidgetPos = ui->tweetLabel->pos();
  qreal opacity = qreal(ui->tweetLabel->width() - ui->tweetLabel->pos().x()) / ui->tweetLabel->width();
  if (opacity > 1.0)
    opacity = 2.0 - opacity;
  d->opacityEffect->setOpacity(opacity - 0.25);
}

bool MainWindow::eventFilter(QObject *obj, QEvent *event)
{
  Q_D(MainWindow);
  switch (event->type()) {
  case QEvent::MouseButtonPress:
  {
    if (obj->objectName() == ui->tweetLabel->objectName()) {
      QMouseEvent *mouseEvent = reinterpret_cast<QMouseEvent*>(event);
      if (mouseEvent->button() == Qt::LeftButton) {
        d->lastWidgetPos = ui->tweetLabel->pos();
        d->lastMousePos = mouseEvent->globalPos();
        d->mouseDown = true;
        ui->tweetLabel->setCursor(Qt::ClosedHandCursor);
        d->mouseMoveTimer.start();
        d->kineticData.clear();
      }
    }
    break;
  }
  case QEvent::MouseMove:
  {
    QMouseEvent *mouseEvent = reinterpret_cast<QMouseEvent*>(event);
    if (d->mouseDown && obj->objectName() == ui->tweetLabel->objectName()) {
      scrollBy(QPoint(mouseEvent->globalPos().x() - d->lastMousePos.x(), 0));
      d->kineticData.append(KineticData(mouseEvent->globalPos(), d->mouseMoveTimer.elapsed()));
      if (d->kineticData.size() > MaxKineticDataSamples)
        d->kineticData.erase(d->kineticData.begin());
      d->lastMousePos = mouseEvent->globalPos();
    }
    break;
  }
  case QEvent::MouseButtonRelease:
  {
    if (obj->objectName() == ui->tweetLabel->objectName()) {
      QMouseEvent *mouseEvent = reinterpret_cast<QMouseEvent*>(event);
      if (mouseEvent->button() == Qt::LeftButton) {
        d->mouseDown = false;
        ui->tweetLabel->setCursor(Qt::OpenHandCursor);
        if (d->kineticData.count() == MaxKineticDataSamples) {
          int timeSinceLastMoveEvent = d->mouseMoveTimer.elapsed() - d->kineticData.last().t;
          if (timeSinceLastMoveEvent < 100) {
            int dt = d->mouseMoveTimer.elapsed() - d->kineticData.first().t;
            const QPointF& moveDist = QPointF(mouseEvent->globalPos().x() - d->kineticData.first().p.x(), 0);
            const QPointF& initialVector = 1000 * moveDist / dt / TimeInterval;
            startMotion(initialVector);
          }
        }
      }
    }
    break;
  }
  default:
    break;
  }
  return QObject::eventFilter(obj, event);
}


void MainWindow::onLinkedChanged(void)
{
  Q_D(MainWindow);
  qDebug() << "MainWindow::onLinkedChanged()" << d->oauth->linked();
}


void MainWindow::onLinkingFailed(void)
{
  qWarning() << "MainWindow::onLinkingFailed()";
}


void MainWindow::onLinkingSucceeded(void)
{
  Q_D(MainWindow);
  O1Twitter* o1t = qobject_cast<O1Twitter*>(sender());
  if (!o1t->extraTokens().isEmpty()) {
    d->settings.setValue("twitter/screenName", o1t->extraTokens().value("screen_name"));
    d->settings.setValue("twitter/userId", o1t->extraTokens().value("user_id"));
    d->settings.sync();
  }
  if (d->oauth->linked()) {
    ui->screenNameLineEdit->setText(d->settings.value("twitter/screenName").toString());
    ui->userIdLineEdit->setText(d->settings.value("twitter/userId").toString());
  }
  else {
    ui->screenNameLineEdit->setText(QString());
    ui->userIdLineEdit->setText(QString());
  }
  getUserTimeline();
}


void MainWindow::onOpenBrowser(const QUrl &url)
{
  ui->statusBar->showMessage(tr("Opening browser: %1").arg(url.toString()), 3000);
  QDesktopServices::openUrl(url);
}


void MainWindow::onCloseBrowser(void)
{
  ui->statusBar->showMessage(tr("Closing browser"), 3000);
}


QJsonDocument MainWindow::mergeTweets(const QJsonDocument &storedJson, const QJsonDocument &currentJson)
{
  Q_D(MainWindow);
  const QList<QVariant> &storedList = storedJson.toVariant().toList();
  const QList<QVariant> &currentList = currentJson.toVariant().toList();
  ui->statusBar->showMessage(tr("%1 new entries since id %2").arg(currentList.size()).arg(d->lastId), 3000);

  auto idComparator = [](const QVariant &a, const QVariant &b) {
    return a.toMap()["id"].toLongLong() > b.toMap()["id"].toLongLong();
  };

  QList<QVariant> result = storedList;
  foreach (QVariant post, currentList) {
    QList<QVariant>::const_iterator idx;
    idx = qBinaryFind(storedList.constBegin(), storedList.constEnd(), post, idComparator);
    if (idx == storedList.end()) {
      result << post;
    }
  }
  qSort(result.begin(), result.end(), idComparator);
  return QJsonDocument::fromVariant(result);
}


void MainWindow::pickNextTweet(void)
{
  ui->tweetLabel->setText(ui->tableWidget->itemAt(0, 0)->text());
  ui->tableWidget->removeRow(0);
}


void MainWindow::gotUserTimeline(QNetworkReply *reply)
{
  Q_D(MainWindow);
  if (reply->error() != QNetworkReply::NoError) {
    ui->statusBar->showMessage(tr("Error: %1").arg(reply->errorString()));
  }
  else {
    QJsonDocument currentTweets = QJsonDocument::fromJson(reply->readAll());
    d->storedTweets = mergeTweets(d->storedTweets, currentTweets);
    d->lastId = d->storedTweets.toVariant().toList().isEmpty() ? 0 : d->storedTweets.toVariant().toList().first().toMap()["id"].toLongLong();

    QFile tweetFile(d->tweetFilename);
    tweetFile.open(QIODevice::WriteOnly | QIODevice::Truncate);
    tweetFile.write(d->storedTweets.toJson(QJsonDocument::Indented));
    tweetFile.close();

    QList<QVariant> posts = d->storedTweets.toVariant().toList();
    ui->tableWidget->setRowCount(posts.count());
    qDebug() << "total number of tweets:" << posts.count();
    int row = 0;
    foreach(QVariant p, posts) {
      const QVariantMap &post = p.toMap();
      ui->tableWidget->setItem(row, 0, new QTableWidgetItem(post["text"].toString()));
      ui->tableWidget->setItem(row, 1, new QTableWidgetItem(post["created_at"].toString()));
      ui->tableWidget->setItem(row, 2, new QTableWidgetItem(QString("%1").arg(post["id"].toLongLong())));
      ++row;
    }
    ui->tableWidget->resizeColumnToContents(0);
    pickNextTweet();
  }
}


void MainWindow::onLogout(void)
{
  Q_D(MainWindow);
  d->oauth->unlink();
}


void MainWindow::onLogin(void)
{
  Q_D(MainWindow);
  d->oauth->link();
}


void MainWindow::getUserTimeline(void)
{
  Q_D(MainWindow);
  if (d->oauth->linked()) {
    O1Requestor *requestor = new O1Requestor(&d->NAM, d->oauth, this);
    QList<O1RequestParameter> reqParams;
    if (d->lastId > 0)
      reqParams << O1RequestParameter("since_id", QString::number(d->lastId).toLatin1());
    else
      reqParams << O1RequestParameter("count", "200");
    reqParams << O1RequestParameter("trim_user", "true");
    QNetworkRequest request(QUrl("https://api.twitter.com/1.1/statuses/home_timeline.json"));
    request.setHeader(QNetworkRequest::ContentTypeHeader, O2_MIME_TYPE_XFORM);
    d->reply = requestor->get(request, reqParams);
  }
  else {
    ui->statusBar->showMessage(tr("Application is not linked to Twitter."));
  }
}


void MainWindow::saveSettings(void)
{
  Q_D(MainWindow);
  d->settings.setValue("mainwindow/geometry", saveGeometry());
  d->settings.setValue("mainwindow/state", saveState());
  d->settings.sync();
}


void MainWindow::restoreSettings(void)
{
  Q_D(MainWindow);
  restoreGeometry(d->settings.value("mainwindow/geometry").toByteArray());
  restoreState(d->settings.value("mainwindow/state").toByteArray());
}
