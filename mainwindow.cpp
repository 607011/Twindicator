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
#include <QDesktopServices>

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
    : o1(new O1Twitter(parent))
  { /* ... */ }
  ~MainWindowPrivate()
  { /* ... */ }

  O1Twitter *o1;
  QVariantMap extraTokens;
};


MainWindow::MainWindow(QWidget *parent)
  : QMainWindow(parent)
  , ui(new Ui::MainWindow)
  , d_ptr(new MainWindowPrivate(this))
{
  Q_D(MainWindow);
  ui->setupUi(this);

  d->o1->setClientId(MY_CLIENT_KEY);
  d->o1->setClientSecret(MY_CLIENT_SECRET);
  d->o1->setLocalPort(44444);

  connect(d->o1, SIGNAL(linkedChanged()), SLOT(onLinkedChanged()));
  connect(d->o1, SIGNAL(linkingFailed()), SLOT(onLinkingFailed()));
  connect(d->o1, SIGNAL(linkingSucceeded()), SLOT(onLinkingSucceeded()));
  connect(d->o1, SIGNAL(openBrowser(QUrl)),SLOT(onOpenBrowser(QUrl)));
  connect(d->o1, SIGNAL(closeBrowser()), SLOT(onCloseBrowser()));

  O2SettingsStore *store = new O2SettingsStore(O2_ENCRYPTION_KEY);
  store->setGroupKey("twitter");
  d->o1->setStore(store);

  d->o1->link();
}


MainWindow::~MainWindow()
{
  delete ui;
}


void MainWindow::onLinkedChanged(void)
{
  Q_D(MainWindow);
  qDebug() << "MainWindow::onLinkedChanged()" << d->o1->linked();
}


void MainWindow::onLinkingFailed(void)
{
  qDebug() << "MainWindow::onLinkingFailed()";
}


void MainWindow::onLinkingSucceeded(void)
{
  Q_D(MainWindow);
  qDebug() << "MainWindow::onLinkingSucceeded()";
  O1Twitter* o1t = qobject_cast<O1Twitter *>(sender());
  d->extraTokens = o1t->extraTokens();
  if (!d->extraTokens.isEmpty()) {
    qDebug() << "Extra tokens in response:";
    foreach (QString key, d->extraTokens.keys()) {
      qDebug() << "\t" << key << ":" << d->extraTokens.value(key).toString();
    }
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


void MainWindow::getUserTimelineDone(void)
{
  QNetworkReply *reply = qobject_cast<QNetworkReply *>(sender());
  if (reply->error() != QNetworkReply::NoError) {
    qDebug() << "ERROR:" << reply->errorString();
  } else {
    qDebug() << "content:" << reply->readAll();
  }
}


void MainWindow::getUserTimeline(void)
{
  Q_D(MainWindow);
  if (d->o1->linked()) {
    QNetworkAccessManager* manager = new QNetworkAccessManager(this);
    O1Twitter* o1 = d->o1;
    O1Requestor* requestor = new O1Requestor(manager, o1, this);
    QList<O1RequestParameter> reqParams = QList<O1RequestParameter>();
    QNetworkRequest request(QUrl("https://api.twitter.com/1.1/statuses/user_timeline.json"));
    request.setHeader(QNetworkRequest::ContentTypeHeader, O2_MIME_TYPE_XFORM);
    QNetworkReply *reply = requestor->get(request, reqParams);
    connect(reply, SIGNAL(finished()), this, SLOT(getUserTimelineDone()));
  }
  else {
    qWarning() << "Application is not linked to Twitter!";
  }
}
