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
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QDateTime>
#include <QDesktopServices>
#include <QJsonDocument>
#include <QDir>
#include <QFile>

#include "globals.h"
#include "mainwindow.h"
#include "ui_mainwindow.h"

#include "o1twitter.h"
#include "o1requestor.h"
#include "o2globals.h"
#include "o2settingsstore.h"


class MainWindowPrivate
{
public:
  MainWindowPrivate(QObject *parent)
    : oauth(new O1Twitter(parent))
    , store(new O2SettingsStore(O2_ENCRYPTION_KEY))
    , settings(QSettings::IniFormat, QSettings::UserScope, AppCompanyName, AppName)
    , NAM(parent)
  {
    oauth->setClientId(MY_CLIENT_KEY);
    oauth->setClientSecret(MY_CLIENT_SECRET);
    oauth->setLocalPort(44333);
    oauth->setSignatureMethod(O2_SIGNATURE_TYPE_HMAC_SHA1);
    store->setGroupKey("twitter");
    oauth->setStore(store);
  }
  ~MainWindowPrivate()
  {
    /* ... */
  }

  O1Twitter *oauth;
  O2SettingsStore *store;
  QSettings settings;
  QNetworkAccessManager NAM;
  QString tweetFilepath;
  QString tweetFilename;
  QString badTweetFilename;
  QString goodTweetFilename;
};


MainWindow::MainWindow(QWidget *parent)
  : QMainWindow(parent)
  , ui(new Ui::MainWindow)
  , d_ptr(new MainWindowPrivate(this))
{
  Q_D(MainWindow);
  ui->setupUi(this);

  d->tweetFilepath = QStandardPaths::writableLocation(QStandardPaths::DataLocation);
  QDir().mkpath(d->tweetFilepath);

  QObject::connect(d->oauth, SIGNAL(linkedChanged()), SLOT(onLinkedChanged()));
  QObject::connect(d->oauth, SIGNAL(linkingFailed()), SLOT(onLinkingFailed()));
  QObject::connect(d->oauth, SIGNAL(linkingSucceeded()), SLOT(onLinkingSucceeded()));
  QObject::connect(d->oauth, SIGNAL(openBrowser(QUrl)), SLOT(onOpenBrowser(QUrl)));
  QObject::connect(d->oauth, SIGNAL(closeBrowser()), SLOT(onCloseBrowser()));
  QObject::connect(ui->actionExit, SIGNAL(triggered(bool)), SLOT(close()));
  QObject::connect(ui->actionLogout, SIGNAL(triggered(bool)), SLOT(onLogout()));

  QObject::connect(ui->tableWidget, SIGNAL(cellClicked(int,int)), SLOT(onCellPressed(int,int)));
  ui->tableWidget->installEventFilter(this);

  restoreSettings();
  d->oauth->link();
}


MainWindow::~MainWindow()
{
  delete ui;
}


void MainWindow::closeEvent(QCloseEvent*)
{
  saveSettings();
}


bool MainWindow::eventFilter(QObject *obj, QEvent *event)
{
  switch (event->type()) {
  case QEvent::MouseButtonPress:
  {
    QMouseEvent *mouseEvent = reinterpret_cast<QMouseEvent*>(event);
    qDebug() << mouseEvent->pos();
    if (obj->objectName() == ui->tableWidget->objectName()) {
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
    QObject::connect(ui->actionLogout, SIGNAL(triggered(bool)), this, SLOT(onLogout()));
    QObject::disconnect(ui->actionLogin, SIGNAL(triggered(bool)), this, SLOT(onLogin()));
    ui->actionLogout->setEnabled(true);
    ui->actionLogin->setEnabled(false);
    ui->screenNameLineEdit->setText(d->settings.value("twitter/screenName").toString());
    ui->userIdLineEdit->setText(d->settings.value("twitter/userId").toString());
    d->tweetFilename = d->tweetFilepath + "/all_tweets_of_" + d->settings.value("twitter/userId").toString() + ".json";
    d->badTweetFilename = d->tweetFilepath + "/bad_tweets_of_" + d->settings.value("twitter/userId").toString() + ".json";
    d->goodTweetFilename = d->tweetFilepath + "/good_tweets_of_" + d->settings.value("twitter/userId").toString() + ".json";
  }
  else {
    QObject::disconnect(ui->actionLogout, SIGNAL(triggered(bool)), this, SLOT(onLogout()));
    QObject::connect(ui->actionLogin, SIGNAL(triggered(bool)), this, SLOT(onLogin()));
    ui->actionLogout->setEnabled(false);
    ui->actionLogin->setEnabled(true);
    ui->screenNameLineEdit->setText(QString());
    ui->userIdLineEdit->setText(QString());
  }
  getUserTimeline();
}


void MainWindow::onOpenBrowser(const QUrl &url)
{
  qDebug() << "MainWindow::onOpenBrowser()" << url;
  QDesktopServices::openUrl(url);
}


void MainWindow::onCloseBrowser(void)
{
  qDebug() << "MainWindow::onCloseBrowser()";
}


QJsonDocument MainWindow::mergeTweets(const QJsonDocument &storedJson, const QJsonDocument &currentJson)
{
  QList<QVariant> stored = storedJson.toVariant().toList();
  QList<QVariant> current = currentJson.toVariant().toList();

  static const int NotFound = -1;

  auto indexOf = [](const QString &id, const QList<QVariant> &posts) {
    int idx = 0;
    foreach (QVariant post, posts) {
      if (post.toMap()["id_str"].toString() == id)
        return idx;
      ++idx;
    }
    return NotFound;
  };

  QList<QVariant> result = stored;
  foreach (QVariant post, current) {
    const QString &id = post.toMap()["id_str"].toString();
    int idx = indexOf(id, stored);
    if (idx == NotFound) {
      qDebug().nospace() << "Added post #" << id;
      result << post;
    }
  }

  qSort(result.begin(), result.end(), [](const QVariant &a, const QVariant &b)
  {
    return a.toMap()["id"].toLongLong() > b.toMap()["id"].toLongLong();
  });

  return QJsonDocument::fromVariant(result);
}


void MainWindow::getUserTimelineDone(void)
{
  Q_D(MainWindow);

  QNetworkReply *reply = qobject_cast<QNetworkReply *>(sender());
  if (reply->error() != QNetworkReply::NoError) {
    qDebug() << "ERROR:" << reply->errorString();
    qDebug() << reply->readAll();
  }
  else {
    QJsonDocument currentTweets = QJsonDocument::fromJson(reply->readAll());
    QJsonDocument allTweets;
    /* From the Qt docs: "QStandardPaths::DataLocation returns the
     * same value as AppLocalDataLocation. This enumeration value
     * is deprecated. Using AppDataLocation is preferable since on
     * Windows, the roaming path is recommended."
     *
     * AppLocalDataLocation was introduced in Qt 5.4. To maintain
     * compatibility to Qt 5.3 (which is found in many reasonably
     * current Linux distributions this code still uses the
     * deprecated value DataLocation.
     */
    QFile tweetFile(d->tweetFilename);
    tweetFile.open(QIODevice::ReadOnly);
    QJsonDocument storedTweets = QJsonDocument::fromJson(tweetFile.readAll());
    tweetFile.close();
    allTweets = mergeTweets(storedTweets, currentTweets);
    tweetFile.open(QIODevice::WriteOnly | QIODevice::Truncate);
    tweetFile.write(allTweets.toJson(QJsonDocument::Indented));
    tweetFile.close();

    QList<QVariant> posts = allTweets.toVariant().toList();
    ui->tableWidget->setRowCount(posts.count());
    qDebug() << "total number of tweets:" << posts.count();
    int row = 0;
    foreach(QVariant p, posts) {
      QVariantMap post = p.toMap();
      QTableWidgetItem *itemId = new QTableWidgetItem(QString("%1").arg(post["id"].toLongLong()));
      QTableWidgetItem *itemCreatedAt = new QTableWidgetItem(post["created_at"].toString());
      QTableWidgetItem *itemText = new QTableWidgetItem(post["text"].toString());
      ui->tableWidget->setItem(row, 0, itemText);
      ui->tableWidget->setItem(row, 1, itemCreatedAt);
      ui->tableWidget->setItem(row, 2, itemId);
      ++row;
    }
    ui->tableWidget->resizeColumnToContents(0);
    ui->tableWidget->verticalHeader()->hide();
  }
}


void MainWindow::onCellPressed(int row, int column)
{
  qDebug() << row << column;
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
    reqParams << O1RequestParameter("count", "200");
    reqParams << O1RequestParameter("trim_user", "true");
    QNetworkRequest request(QUrl("https://api.twitter.com/1.1/statuses/home_timeline.json"));
    request.setHeader(QNetworkRequest::ContentTypeHeader, O2_MIME_TYPE_XFORM);
    QNetworkReply *reply = requestor->get(request, reqParams);
    connect(reply, SIGNAL(finished()), this, SLOT(getUserTimelineDone()));
  }
  else {
    qWarning() << "Application is not linked to Twitter!";
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
