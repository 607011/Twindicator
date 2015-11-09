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
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QDateTime>
#include <QDesktopServices>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QDir>
#include <QFile>
#include <QPoint>
#include <QGraphicsOpacityEffect>
#include <QEasingCurve>
#include <QPropertyAnimation>
#include <QTime>
#include <QTimer>
#include <QVector>
#include <QShortcut>
#include <QLabel>
#include <QRegExp>
#include <qmath.h>

#include "globals.h"
#include "mainwindow.h"
#include "flowlayout.h"
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


auto wordComparator = [](const QString &a, const QString &b) {
  return a.compare(b, Qt::CaseInsensitive) < 0;
};


auto idComparator = [](const QVariant &a, const QVariant &b) {
  return a.toMap()["id"].toLongLong() > b.toMap()["id"].toLongLong();
};



static const int MaxKineticDataSamples = 5;
static const qreal Friction = 0.95;
static const int TimeInterval = 25;
static const int AnimationDuration = 200;


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
    , tweetFrameOpacityEffect(Q_NULLPTR)
    , mouseMoveTimerId(0)
    , tableBuildCalled(false)
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
    , mostRecentId(0)
  {
    store->setGroupKey("twitter");
    oauth->setStore(store);
    oauth->setClientId(MY_CLIENT_KEY);
    oauth->setClientSecret(MY_CLIENT_SECRET);
    oauth->setLocalPort(44333);
    oauth->setSignatureMethod(O2_SIGNATURE_TYPE_HMAC_SHA1);
    floatInAnimation.setPropertyName("pos");
    floatInAnimation.setEasingCurve(QEasingCurve::InOutQuad);
    floatInAnimation.setDuration(AnimationDuration);
    floatOutAnimation.setPropertyName("pos");
    floatOutAnimation.setEasingCurve(QEasingCurve::InQuad);
    floatOutAnimation.setDuration(AnimationDuration);
    unfloatAnimation.setPropertyName("pos");
    unfloatAnimation.setDuration(AnimationDuration);
    unfloatAnimation.setEasingCurve(QEasingCurve::InOutQuad);
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
  QString wordListFilename;
  QJsonArray storedTweets;
  QJsonArray badTweets;
  QJsonArray goodTweets;
  qlonglong mostRecentId;
  QPoint originalTweetFramePos;
  QPoint lastTweetFramePos;
  QPoint lastMousePos;
  bool mouseDown;
  QGraphicsOpacityEffect *tweetFrameOpacityEffect;
  QTime mouseMoveTimer;
  int mouseMoveTimerId;
  QPointF velocity;
  QJsonValue currentTweet;
  QPropertyAnimation unfloatAnimation;
  QPropertyAnimation floatInAnimation;
  QPropertyAnimation floatOutAnimation;
  bool tableBuildCalled;
  QStringList relevantWords;
  QMenu *tableContextMenu;
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
  d->wordListFilename = d->tweetFilepath + "/relevant_words_of_" + d->settings.value("twitter/userId").toString() + ".txt";

  bool ok;

  ok = QDir().mkpath(d->tweetFilepath);

  QFile tweetFile(d->tweetFilename);
  ok = tweetFile.open(QIODevice::ReadOnly);
  if (ok) {
    d->storedTweets = QJsonDocument::fromJson(tweetFile.readAll()).array();
    tweetFile.close();
  }
  QFile badTweetsFile(d->badTweetFilename);
  ok = badTweetsFile.open(QIODevice::ReadOnly);
  if (ok) {
    d->badTweets = QJsonDocument::fromJson(badTweetsFile.readAll()).array();
    badTweetsFile.close();
  }
  QFile goodTweets(d->goodTweetFilename);
  ok = goodTweets.open(QIODevice::ReadOnly);
  if (ok) {
    d->goodTweets = QJsonDocument::fromJson(goodTweets.readAll()).array();
    goodTweets.close();
  }
  QFile wordList(d->wordListFilename);
  ok = wordList.open(QIODevice::ReadOnly);
  if (ok) {
    while (!wordList.atEnd()) {
      QString word = QString::fromUtf8(wordList.readLine());
      d->relevantWords << word.trimmed();
    }
    wordList.close();
    qSort(d->relevantWords.begin(), d->relevantWords.end(), wordComparator);
  }

  QObject::connect(d->oauth, SIGNAL(linkedChanged()), SLOT(onLinkedChanged()));
  QObject::connect(d->oauth, SIGNAL(linkingFailed()), SLOT(onLinkingFailed()));
  QObject::connect(d->oauth, SIGNAL(linkingSucceeded()), SLOT(onLinkingSucceeded()));
  QObject::connect(d->oauth, SIGNAL(openBrowser(QUrl)), SLOT(onOpenBrowser(QUrl)));
  QObject::connect(d->oauth, SIGNAL(closeBrowser()), SLOT(onCloseBrowser()));
  QObject::connect(ui->likeButton, SIGNAL(clicked(bool)), SLOT(like()));
  QObject::connect(ui->dislikeButton, SIGNAL(clicked(bool)), SLOT(dislike()));
  QObject::connect(ui->actionExit, SIGNAL(triggered(bool)), SLOT(close()));
  QObject::connect(ui->actionRefresh, SIGNAL(triggered(bool)), SLOT(getUserTimeline()));
  ui->tweetFrame->installEventFilter(this);
  d->tweetFrameOpacityEffect = new QGraphicsOpacityEffect(ui->tweetFrame);
  d->tweetFrameOpacityEffect->setOpacity(1.0);
  ui->tweetFrame->setGraphicsEffect(d->tweetFrameOpacityEffect);
  d->floatOutAnimation.setTargetObject(ui->tweetFrame);
  d->floatInAnimation.setTargetObject(ui->tweetFrame);
  d->unfloatAnimation.setTargetObject(ui->tweetFrame);

  ui->likeButton->stackUnder(ui->tweetFrame);
  ui->dislikeButton->stackUnder(ui->tweetFrame);

  QObject::connect(&d->NAM, SIGNAL(finished(QNetworkReply*)), this, SLOT(gotUserTimeline(QNetworkReply*)));

  ui->tableWidget->verticalHeader()->hide();
  ui->tableWidget->setContextMenuPolicy(Qt::CustomContextMenu);
  QObject::connect(ui->tableWidget, SIGNAL(customContextMenuRequested(QPoint)), SLOT(onCustomMenuRequested(QPoint)));
  d->tableContextMenu = new QMenu(ui->tableWidget);
  d->tableContextMenu->addAction(tr("Delete"), this, SLOT(onDeleteTweet()));
  d->tableContextMenu->addAction(tr("Evaluate"), this, SLOT(onEvaluateTweet()));

  restoreSettings();

  d->oauth->link();

  QTimer::singleShot(10, this, SLOT(buildTable()));

  new QShortcut(QKeySequence(QKeySequence::MoveToNextChar), this, SLOT(like()));
  new QShortcut(QKeySequence(QKeySequence::MoveToPreviousChar), this, SLOT(dislike()));
}


MainWindow::~MainWindow()
{
  delete ui;
}


void MainWindow::showEvent(QShowEvent *)
{
  Q_D(MainWindow);
  d->originalTweetFramePos = ui->tweetFrame->pos();
}


void MainWindow::closeEvent(QCloseEvent*)
{
  Q_D(MainWindow);

  stopMotion();
  saveSettings();

  QFile tweetFile(d->tweetFilename);
  tweetFile.open(QIODevice::WriteOnly | QIODevice::Truncate);
  tweetFile.write(QJsonDocument(d->storedTweets).toJson(QJsonDocument::Indented));
  tweetFile.close();

  QFile badTweetFile(d->badTweetFilename);
  badTweetFile.open(QIODevice::WriteOnly | QIODevice::Truncate);
  badTweetFile.write(QJsonDocument(d->badTweets).toJson(QJsonDocument::Indented));
  badTweetFile.close();

  QFile goodTweetFile(d->goodTweetFilename);
  goodTweetFile.open(QIODevice::WriteOnly | QIODevice::Truncate);
  goodTweetFile.write(QJsonDocument(d->goodTweets).toJson(QJsonDocument::Indented));
  goodTweetFile.close();

  QFile wordFile(d->wordListFilename);
  wordFile.open(QIODevice::WriteOnly | QIODevice::Truncate);
  qSort(d->relevantWords.begin(), d->relevantWords.end(), wordComparator);
  foreach (QString word, d->relevantWords)
    wordFile.write(word.toUtf8() + "\n");
  wordFile.close();
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
      if (tweetFloating())
        unfloatTweet();
    }
  }
}


void MainWindow::unfloatTweet(void)
{
  Q_D(MainWindow);
  d->unfloatAnimation.setStartValue(ui->tweetFrame->pos());
  d->unfloatAnimation.setEndValue(d->originalTweetFramePos);
  d->unfloatAnimation.start();
  d->tweetFrameOpacityEffect->setOpacity(1.0);
}


void MainWindow::startMotion(const QPointF &velocity)
{
  Q_D(MainWindow);
  d->lastTweetFramePos = ui->tweetFrame->pos();
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


int MainWindow::likeLimit(void) const
{
  return ui->tweetFrame->width();
}


int MainWindow::dislikeLimit(void) const
{
  return -likeLimit();
}


bool MainWindow::tweetFloating(void) const
{
  const int x = ui->tweetFrame->pos().x();
  return dislikeLimit() < x && x < likeLimit();
}


void MainWindow::scrollBy(const QPoint &offset)
{
  Q_D(MainWindow);
  ui->tweetFrame->move(offset.x() + d->lastTweetFramePos.x(), d->lastTweetFramePos.y());
  d->lastTweetFramePos = ui->tweetFrame->pos();
  qreal opacity = qreal(ui->tweetFrame->width() - ui->tweetFrame->pos().x()) / ui->tweetFrame->width();
  if (opacity > 1.0)
    opacity = 2.0 - opacity;
  d->tweetFrameOpacityEffect->setOpacity(opacity - 0.25);
  if (ui->tweetFrame->pos().x() < dislikeLimit()) {
    dislike();
  }
  else if (ui->tweetFrame->pos().x() > likeLimit()) {
    like();
  }
}


bool MainWindow::eventFilter(QObject *obj, QEvent *event)
{
  Q_D(MainWindow);
  switch (event->type()) {
  case QEvent::MouseButtonPress:
  {
    if (obj->objectName() == ui->tweetFrame->objectName()) {
      QMouseEvent *mouseEvent = reinterpret_cast<QMouseEvent*>(event);
      if (mouseEvent->button() == Qt::LeftButton) {
        d->lastTweetFramePos = ui->tweetFrame->pos();
        d->lastMousePos = mouseEvent->globalPos();
        d->mouseDown = true;
        ui->tweetFrame->setCursor(Qt::ClosedHandCursor);
        d->mouseMoveTimer.start();
        d->kineticData.clear();
      }
    }
    break;
  }
  case QEvent::MouseMove:
  {
    QMouseEvent *mouseEvent = reinterpret_cast<QMouseEvent*>(event);
    if (d->mouseDown && obj->objectName() == ui->tweetFrame->objectName()) {
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
    if (obj->objectName() == ui->tweetFrame->objectName()) {
      QMouseEvent *mouseEvent = reinterpret_cast<QMouseEvent*>(event);
      if (mouseEvent->button() == Qt::LeftButton) {
        d->mouseDown = false;
        ui->tweetFrame->setCursor(Qt::OpenHandCursor);
        if (d->kineticData.count() == MaxKineticDataSamples) {
          int timeSinceLastMoveEvent = d->mouseMoveTimer.elapsed() - d->kineticData.last().t;
          if (timeSinceLastMoveEvent < 100) {
            int dt = d->mouseMoveTimer.elapsed() - d->kineticData.first().t;
            const QPointF &moveDist = QPointF(mouseEvent->globalPos().x() - d->kineticData.first().p.x(), 0);
            const QPointF &initialVector = 1000 * moveDist / dt / TimeInterval;
            startMotion(initialVector);
          }
          else {
            unfloatTweet();
          }
        }
      }
    }
    break;
  }
  case QEvent::LayoutRequest:
    if (obj->objectName() == ui->tweetFrame->objectName()) {
      return false;
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


QJsonArray MainWindow::mergeTweets(const QJsonArray &storedJson, const QJsonArray &currentJson)
{
  const QList<QVariant> &currentList = currentJson.toVariantList();
  const QList<QVariant> &storedList = storedJson.toVariantList();
  QList<QVariant> result = storedList;
  foreach (QVariant post, currentList) {
    QList<QVariant>::const_iterator idx;
    idx = qBinaryFind(storedList.constBegin(), storedList.constEnd(), post, idComparator);
    if (idx == storedList.constEnd()) {
      result << post;
    }
  }
  qSort(result.begin(), result.end(), idComparator);
  return QJsonArray::fromVariantList(result);
}


void MainWindow::calculateMostRecentId(void)
{
  Q_D(MainWindow);
  d->mostRecentId = d->currentTweet.toVariant().toMap()["id"].toLongLong();
  if (!d->storedTweets.isEmpty()) {
    qlonglong id = d->storedTweets.first().toVariant().toMap()["id"].toLongLong();
    d->mostRecentId = qMax(id, d->mostRecentId);
  }
  if (!d->badTweets.isEmpty()) {
    qlonglong id = d->badTweets.last().toVariant().toMap()["id"].toLongLong();
    d->mostRecentId = qMax(id, d->mostRecentId);
  }
  if (!d->goodTweets.isEmpty()) {
    qlonglong id = d->goodTweets.last().toVariant().toMap()["id"].toLongLong();
    d->mostRecentId = qMax(id, d->mostRecentId);
  }
}


void MainWindow::wordSelected(void)
{
  Q_D(MainWindow);
  if (sender() == Q_NULLPTR)
    return;
  QPushButton *btn = reinterpret_cast<QPushButton*>(sender());
  static const QRegExp reWord("([#\\w-']+)");
  reWord.exactMatch(btn->text());
  QString w = reWord.cap().trimmed();
  QStringList::const_iterator idx = qBinaryFind(d->relevantWords.constBegin(), d->relevantWords.constEnd(), w, wordComparator);
  if (idx == d->relevantWords.constEnd()) {
    d->relevantWords << w;
    qSort(d->relevantWords.begin(), d->relevantWords.end(), wordComparator);
    ui->statusBar->showMessage(tr("Added \"%1\" to list of relevant words.").arg(w), 3000);
  }
}


void MainWindow::onCustomMenuRequested(const QPoint &pos)
{
  Q_D(MainWindow);
  d->tableContextMenu->popup(ui->tableWidget->viewport()->mapToGlobal(pos));
  int row = ui->tableWidget->verticalHeader()->logicalIndexAt(pos);
  ui->tableWidget->selectRow(row);
}


void MainWindow::onDeleteTweet(void)
{
  qDebug() << "MainWindow::onDeleteTweet()";
}


void MainWindow::onEvaluateTweet(void)
{
  qDebug() << "MainWindow::onDeleteTweet()";
}


void MainWindow::pickNextTweet(void)
{
  Q_D(MainWindow);
  stopMotion();
  if (ui->tableWidget->columnCount() > 0 && ui->tableWidget->rowCount() > 0) {
    QLayoutItem *item;
    if (ui->tweetFrame->layout()) {
      while ((item = ui->tweetFrame->layout()->takeAt(0)) != Q_NULLPTR) {
        delete item->widget();
        delete item;
      }
      delete ui->tweetFrame->layout();
    }
    d->currentTweet = d->storedTweets.first();
    d->storedTweets.pop_front();
    calculateMostRecentId();
    static const QRegExp delim("\\s", Qt::CaseSensitive, QRegExp::RegExp2);
//    static const QRegExp reUrl("^(https?:\\/\\/)?([\\da-z\\.-]+)\\.([a-z\\.]{2,6})([\\/\\w \\.-]*)*\\/?$", Qt::CaseInsensitive, QRegExp::RegExp2);
    const QStringList &words = ui->tableWidget->itemAt(0, 0)->text().split(delim);
    FlowLayout *flowLayout = new FlowLayout(2, 2, 2);
    foreach (QString word, words) {
      QPushButton *widget = new QPushButton;
      widget->setStyleSheet("border: 1px solid #444; background-color: #ffdab9; padding: 1px 2px; font-size: 11pt");
      widget->setText(word);
      widget->setCursor(Qt::PointingHandCursor);
      QObject::connect(widget, SIGNAL(clicked(bool)), SLOT(wordSelected()));
      flowLayout->addWidget(widget);
    }
    ui->tableWidget->removeRow(0);
    d->floatInAnimation.setStartValue(d->originalTweetFramePos + QPoint(0, ui->tweetFrame->height()));
    d->floatInAnimation.setEndValue(d->originalTweetFramePos);
    d->floatInAnimation.start();
    d->tweetFrameOpacityEffect->setOpacity(1.0);
    ui->tweetFrame->setLayout(flowLayout);
  }
}


void MainWindow::buildTable(const QJsonArray &mostRecentTweets)
{
  Q_D(MainWindow);
  if (!mostRecentTweets.isEmpty()) {
    d->storedTweets = mergeTweets(d->storedTweets, mostRecentTweets);
    ui->statusBar->showMessage(tr("%1 new entries since id %2").arg(mostRecentTweets.size()).arg(d->mostRecentId), 3000);
    QFile tweetFile(d->tweetFilename);
    tweetFile.open(QIODevice::WriteOnly | QIODevice::Truncate);
    tweetFile.write(QJsonDocument(d->storedTweets).toJson(QJsonDocument::Indented));
    tweetFile.close();
  }
  calculateMostRecentId();

  if (d->storedTweets.isEmpty() && !d->tableBuildCalled) {
    getUserTimeline();
    return;
  }
  d->tableBuildCalled = true;

  ui->tableWidget->setRowCount(d->storedTweets.count());
  int row = 0;
  foreach(QJsonValue p, d->storedTweets) {
    const QVariantMap &post = p.toVariant().toMap();
    ui->tableWidget->setItem(row, 0, new QTableWidgetItem(post["text"].toString()));
    ui->tableWidget->setItem(row, 1, new QTableWidgetItem(post["created_at"].toString()));
    ui->tableWidget->setItem(row, 2, new QTableWidgetItem(QString("%1").arg(post["id"].toLongLong())));
    ++row;
  }
  ui->tableWidget->resizeColumnToContents(0);
  pickNextTweet();
}


void MainWindow::buildTable(void)
{
  buildTable(QJsonArray());
}


void MainWindow::gotUserTimeline(QNetworkReply *reply)
{
  Q_D(MainWindow);
  if (reply->error() != QNetworkReply::NoError) {
    ui->statusBar->showMessage(tr("Error: %1").arg(reply->errorString()));
    const QJsonDocument &msg = QJsonDocument::fromJson(reply->readAll());
    const QList<QVariant> &errors = msg.toVariant().toMap()["errors"].toList();
    QString errMsg;
    foreach (QVariant e, errors)
      errMsg += QString("%1 (code: %2)\n").arg(e.toMap()["message"].toString()).arg(e.toMap()["code"].toInt());
    QMessageBox::warning(this, tr("Error"), errMsg);
  }
  else {
    if (!d->currentTweet.isNull()) {
      d->storedTweets.push_front(d->currentTweet);
      d->currentTweet = QJsonValue();
    }
    const QJsonArray &mostRecentTweets = QJsonDocument::fromJson(reply->readAll()).array();
    buildTable(mostRecentTweets);
  }
}


void MainWindow::getUserTimeline(void)
{
  Q_D(MainWindow);
  if (d->oauth->linked()) {
    O1Requestor *requestor = new O1Requestor(&d->NAM, d->oauth, this);
    QList<O1RequestParameter> reqParams;
    reqParams << (d->mostRecentId > 0
                  ? O1RequestParameter("since_id", QString::number(d->mostRecentId).toLatin1())
                  : O1RequestParameter("count", "200"));
    reqParams << O1RequestParameter("trim_user", "true");
    QNetworkRequest request(QUrl("https://api.twitter.com/1.1/statuses/home_timeline.json"));
    request.setHeader(QNetworkRequest::ContentTypeHeader, O2_MIME_TYPE_XFORM);
    d->reply = requestor->get(request, reqParams);
  }
  else {
    ui->statusBar->showMessage(tr("Application is not linked to Twitter."));
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


void MainWindow::like(void)
{
  Q_D(MainWindow);
  stopMotion();
  d->goodTweets.push_front(d->currentTweet);
  d->floatOutAnimation.setStartValue(ui->tweetFrame->pos());
  d->floatOutAnimation.setEndValue(d->originalTweetFramePos + QPoint(3 * ui->tweetFrame->width() * 2, 0));
  d->floatOutAnimation.start();
  QTimer::singleShot(AnimationDuration, this, &MainWindow::pickNextTweet);
}


void MainWindow::dislike(void)
{
  Q_D(MainWindow);
  stopMotion();
  d->badTweets.push_front(d->currentTweet);
  d->floatOutAnimation.setStartValue(ui->tweetFrame->pos());
  d->floatOutAnimation.setEndValue(d->originalTweetFramePos - QPoint(3 * ui->tweetFrame->width() / 2, 0));
  d->floatOutAnimation.start();
  QTimer::singleShot(AnimationDuration, this, &MainWindow::pickNextTweet);
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
