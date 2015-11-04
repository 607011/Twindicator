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
#include "mainwindow.h"
#include "ui_mainwindow.h"

#include "o1twitter.h"
#include "o1requestor.h"

class MainWindowPrivate
{
public:
  MainWindowPrivate(QObject *parent)
    : o1(new O1Twitter(parent))
  { /* ... */ }
  ~MainWindowPrivate()
  { /* ... */ }

  O1Twitter *o1;
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
  d->o1->setAccessTokenUrl();
  connect(d->o1, SIGNAL(linkedChanged()), SLOT(onLinkedChanged()));
  connect(d->o1, SIGNAL(linkingFailed()), SLOT(onLinkingFailed()));
  connect(d->o1, SIGNAL(linkingSucceeded()), SLOT(onLinkingSucceeded()));
  connect(d->o1, SIGNAL(openBrowser(QUrl)),SLOT(onOpenBrowser(QUrl)));
  connect(d->o1, SIGNAL(closeBrowser()), SLOT(onCloseBrowser()));

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
  qDebug() << "MainWindow::onLinkingSucceeded()";
}


void MainWindow::onOpenBrowser(const QUrl &url)
{
  qDebug() << "MainWindow::onOpenBrowser()" << url;
}


void MainWindow::onCloseBrowser(void)
{
  qDebug() << "MainWindow::onCloseBrowser()";
}
