#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QWebEngineView>
#include <QDir>
#include <QResizeEvent>
#include <vector>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTimer>
#include <QLabel>
#include "ais_anal.h"

QT_BEGIN_NAMESPACE
namespace Ui { class MapWindow; }
QT_END_NAMESPACE

class MapWindow : public QMainWindow
{
    Q_OBJECT

public:
    MapWindow(QWidget *parent = nullptr);
    ~MapWindow();

    void addAisMessage(const AisMessage& message);
    void updateShipMarkers();
    void showPlainTextEdit();
    void loadAisMessagesFromResource();

    void updateShipCounterLabel();

private:
    Ui::MapWindow *ui;
    int windowHeight = 0;
    int windowWidth = 0;
    int webMapViewsHeight = 0;
    int webMapViewsWidth = 0;
    int hideOrNot = -1;
    bool isProcessing = false;
    int currentMessageIndex = 0;

    int shipCounter = 0;

    std::vector<AisMessage> aisMessages;
    QVector<QPair<QDateTime, QString>> rawAisMessages;

    QWebEnginePage *WebPages;
    QWebEngineView *WebMapViews;
    QPlainTextEdit *cipherTextEdit;
    QPlainTextEdit *plainTextEdit;
    QPushButton *btnHidePlainTextEdits;
    QPushButton *btnPauseResume;
    QTimer *messageTimer;
    QTimer *timer;

    QLabel *shipCounterLabel;

    QString strMapPaths, strExePaths;
    QDir qDirs;

protected:
    void resizeEvent(QResizeEvent *event) override;

private slots:
    void on_pushButton_LoadBaiduMaps_clicked();
    void on_pushButton_LocateMaps_clicked();
    void on_btnHidePlainTextEdits_clicked();
    void on_btnPauseResume_clicked();
    void processNextMessage();
    void handleWebPageMessage(const QJsonObject& message);
    void updateTime();
    void clearAllMapLabels();
};

#endif // MAPWINDOW_H
