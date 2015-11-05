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


#ifndef __MAINWINDOW_H_
#define __MAINWINDOW_H_

#include <QMainWindow>
#include <QUrl>
#include <QVariantMap>
#include <QEvent>
#include <QCloseEvent>
#include <QNetworkReply>

namespace Ui {
class MainWindow;
}

class MainWindowPrivate;

class MainWindow : public QMainWindow
{
  Q_OBJECT

public:
  explicit MainWindow(QWidget *parent = Q_NULLPTR);
  ~MainWindow();

protected:
  void closeEvent(QCloseEvent*);
  bool eventFilter(QObject *obj, QEvent *event);

private slots:
  void onLinkedChanged(void);
  void onLinkingFailed(void);
  void onLinkingSucceeded(void);
  void onOpenBrowser(const QUrl &url);
  void onCloseBrowser(void);
  void getUserTimeline(void);
  void gotUserTimeline(QNetworkReply*);
  void onLogout(void);
  void onLogin(void);

private:
  Ui::MainWindow *ui;

  QScopedPointer<MainWindowPrivate> d_ptr;
  Q_DECLARE_PRIVATE(MainWindow)
  Q_DISABLE_COPY(MainWindow)

private: // methods
  void saveSettings(void);
  void restoreSettings(void);
  QJsonDocument mergeTweets(const QJsonDocument &a, const QJsonDocument &b);
};

#endif // __MAINWINDOW_H_
