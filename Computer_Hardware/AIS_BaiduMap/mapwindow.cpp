#include "mapwindow.h"
#include "ui_mapwindow.h"
#include <QDebug>
#include <QMessageBox>
#include <QJsonObject>
#include <QJsonDocument>
#include <QJsonArray>
#include <QFile>
#include <QResource>
#include <QTextStream>
#include <QWebChannel>

MapWindow::MapWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MapWindow)
{
    ui->setupUi(this);

    shipCounterLabel = nullptr; // 显式初始化

    WebMapViews = new QWebEngineView(this);
    messageTimer = new QTimer(this);

    windowHeight = this->height();
    windowWidth = this->width();
    webMapViewsHeight = windowHeight - 100;
    webMapViewsWidth = windowWidth - 20;

    // 初始化地图展示区
    WebMapViews->setGeometry(10, 80, webMapViewsWidth, webMapViewsHeight);
    WebPages = WebMapViews->page();

    // 建立与网页的通信通道
    connect(WebPages, &QWebEnginePage::loadFinished, this, [this](bool success) {
        if (!success) {
            QMessageBox::warning(this, "错误", "地图加载失败！(请尝试刷新地图）");
        }
    });

    // 连接网页消息处理
    connect(WebPages, &QWebEnginePage::loadFinished, this, [this]() {
        WebPages->runJavaScript("window.addEventListener('message', function(event) {"
                                "   if (event.data && event.data.type === 'ship_clicked') {"
                                "       qtObject.handleWebPageMessage(event.data);"
                                "   }"
                                "});");
    });

    // 暴露Qt对象给JavaScript
    QWebChannel *channel = new QWebChannel(this);
    channel->registerObject("qtObject", this);
    WebPages->setWebChannel(channel);

    // 连接定时器
    connect(messageTimer, &QTimer::timeout, this, &MapWindow::processNextMessage);

    on_pushButton_LoadBaiduMaps_clicked();
    loadAisMessagesFromResource(); // 预加载AIS报文

    timer = new QTimer(this);
    connect(timer, &QTimer::timeout, this, &MapWindow::updateTime);
    timer->start(1000);  // 每秒钟更新一次时间

    // 初始更新时间
    updateTime();
}

MapWindow::~MapWindow()
{
    delete shipCounterLabel;
    delete ui;
}

void MapWindow::loadAisMessagesFromResource()
{
    QFile file(":/Resources/messages.txt");
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qDebug() << "无法打开资源文件";
        QMessageBox::critical(this, "错误", "资源文件打开失败");
        return;
    }

    QTextStream stream(&file);
    // stream.setCodec("UTF-8"); // 强制使用UTF-8编码
    int validCount = 0;

    while (!stream.atEnd()) {
        QString line = stream.readLine().trimmed();
        if (line.isEmpty()) continue;

        // Skip non-AIS lines
        if (line.contains(QRegularExpression("[\\x00-\\x1F]"))) {
            // qDebug() << "跳过二进制行:" << line.left(30).toUtf8().toHex();
            continue;
        }

        // Check if it's an AIVDM message
        if (line.startsWith("!AIVDM") || line.startsWith("!ABVDM")) {
            QDateTime now = QDateTime::currentDateTime();
            rawAisMessages.append({now, line});
            validCount++;
        }

        if(validCount > 500) {
            break;
        }
    }

    qDebug() << "========================================";
    qDebug() << "总计处理行数:" << rawAisMessages.size();
    qDebug() << "有效AIS报文数:" << validCount;

    // 保底测试数据（必须包含有效坐标）
    if (rawAisMessages.isEmpty()) {
        qWarning() << "注入测试报文（上海港附近坐标）";
        QDateTime now = QDateTime::currentDateTime();
        rawAisMessages.append({now, "!AIVDM,1,1,,A,13HOI:0P0000VOHLCnHQKwvL05Ip,0*23"});  // 有效坐标报文
        rawAisMessages.append({now, "!AIVDM,1,1,,B,133m@ogP00PD>hRMDHHEP0vN20S:,0*2F"});  // 有效坐标报文
    }
}

void MapWindow::updateShipCounterLabel()
{
    // 确保在主线程中执行
    QMetaObject::invokeMethod(this, [this]() {
        // 检查 shipCounterLabel 是否已初始化
        if (!shipCounterLabel) {
            shipCounterLabel = new QLabel(this);
            shipCounterLabel->move(600, 20);  // 设置位置
            shipCounterLabel->setStyleSheet("QLabel {"
                                                   "background-color: #4CAF50; "  // 背景颜色
                                                   "color: white; "               // 字体颜色
                                                   "font-size: 20px; "            // 字体大小
                                                   "font-weight: bold; "          // 字体加粗
                                                   "border: 2px solid #388E3C; "  // 边框颜色
                                                   "border-radius: 10px; "        // 边框圆角
                                                   "padding: 10px; "              // 内边距
                                                   "}");
            shipCounterLabel->show();
        }

        // 更新文本
        QString lblShipCounter = QString("接收有效船舶数：%1").arg(shipCounter);
        if (shipCounterLabel->text() != lblShipCounter) {
            shipCounterLabel->setText(lblShipCounter);
        }

        // 调整 QLabel 大小以适应文本
        shipCounterLabel->adjustSize();
    }, Qt::QueuedConnection);
}

void MapWindow::addAisMessage(const AisMessage &message)
{
    // 查找是否已存在相同MMSI的船舶
    auto it = std::find_if(aisMessages.begin(), aisMessages.end(),
                           [&message](const AisMessage& m) { return m.mmsi == message.mmsi; });

    if (it != aisMessages.end()) {
        *it = message; // 更新现有船舶
    } else {
        aisMessages.push_back(message); // 添加新船舶

        shipCounter++;
        updateShipCounterLabel();
    }
}

void MapWindow::updateShipMarkers()
{
    // 使用局部变量减少锁定时间
    std::vector<AisMessage> messagesCopy;
    {
        messagesCopy = aisMessages;
    }

    QJsonArray shipsArray;
    qDebug() << "准备更新船舶标记，当前船舶数:" << messagesCopy.size();

    for (const auto& msg : messagesCopy) {
        // 验证坐标有效性
        if (qIsNaN(msg.latitude) || qIsNaN(msg.longitude) ||
            msg.longitude < -180 || msg.longitude > 180 ||
            msg.latitude < -90 || msg.latitude > 90) {
            //qWarning() << "无效坐标被跳过";
            continue;
        }

        QJsonObject ship;
        ship["mmsi"] = msg.mmsi;
        ship["lat"] = msg.latitude;
        ship["lng"] = msg.longitude;
        ship["cog"] = msg.cog;
        ship["name"] = msg.name.isEmpty() ? "MMSI:" + msg.mmsi : msg.name;
        shipsArray.append(ship);
    }

    // 如果没有有效数据，添加测试数据
    if (shipsArray.isEmpty()) {
        qWarning() << "无有效船舶数据，注入测试数据";
        QJsonObject testShip;
        testShip["mmsi"] = "TEST123";
        testShip["lat"] = 30.68028;
        testShip["lng"] = 122.311858;
        testShip["cog"] = 45;
        shipsArray.append(testShip);
    }

    // 转换为JSON字符串
    QString jsonStr = QJsonDocument(shipsArray).toJson();

    // 使用QMetaObject::invokeMethod确保在UI线程执行
    QMetaObject::invokeMethod(this, [this, jsonStr]() {
        QString js = QString(
                         "try {"
                         "   if (typeof updateShipMarkers === 'function') {"
                         "       updateShipMarkers(%1);"
                         "   } else {"
                         "       console.error('updateShipMarkers函数未定义!');"
                         "   }"
                         "} catch(e) { console.error('JS执行错误:', e); }"
                         ).arg(jsonStr);

        WebPages->runJavaScript(js, [](const QVariant &result) {
            // qDebug() << "JavaScript调用完成";
        });
    }, Qt::QueuedConnection);
}

void MapWindow::showPlainTextEdit()
{
    if(cipherTextEdit){
        delete cipherTextEdit;
        delete plainTextEdit;
    }
    cipherTextEdit = new QPlainTextEdit(this);
    plainTextEdit = new QPlainTextEdit(this);

    // 设置文本框位置和大小
    cipherTextEdit->setGeometry(webMapViewsWidth - 300, 90, 300, 300);
    cipherTextEdit->setPlaceholderText("加密文本...");

    plainTextEdit->setGeometry(webMapViewsWidth - 300, 400, 300, 300);
    plainTextEdit->setPlaceholderText("解密文本...");

    // 设置为只读文本框
    cipherTextEdit->setReadOnly(true);
    plainTextEdit->setReadOnly(true);

    // 设置样式
    QString textEditStyle = "font-size: 14px; "
                            "background-color: #f0f0f0; "
                            "border: 2px solid #aaa; "
                            "border-radius: 15px; "
                            "padding: 10px; "
                            "color: #333; "
                            "selection-background-color: #5b9bd5; "
                            "selection-color: white;";

    cipherTextEdit->setStyleSheet(textEditStyle);
    plainTextEdit->setStyleSheet(textEditStyle);

    // 确保控件可见
    cipherTextEdit->show();
    plainTextEdit->show();
    cipherTextEdit->raise();
    plainTextEdit->raise();

    // 创建隐藏/显示按钮
    btnHidePlainTextEdits = new QPushButton("隐藏报文展示框", this);
    btnHidePlainTextEdits->setGeometry(webMapViewsWidth - 90, 10, 100, 50);

    // 设置样式
    QString btnStyle = "font-size: 12px; ";

    btnHidePlainTextEdits->setStyleSheet(btnStyle);
    btnHidePlainTextEdits->show();
    connect(btnHidePlainTextEdits, &QPushButton::clicked, this, &MapWindow::on_btnHidePlainTextEdits_clicked);

    // 创建暂停/继续按钮
    btnPauseResume = new QPushButton("暂停接收", this);
    btnPauseResume->setGeometry(webMapViewsWidth - 200, 10, 100, 50);
    btnPauseResume->show();
    connect(btnPauseResume, &QPushButton::clicked, this, &MapWindow::on_btnPauseResume_clicked);

    hideOrNot = 0;
}

void MapWindow::on_pushButton_LoadBaiduMaps_clicked()
{
    // 改为使用文件系统绝对路径测试
    QString mapPath = QApplication::applicationDirPath() + "./resources/map.html";
    if(QFile::exists(mapPath)) {
        WebPages->load(QUrl::fromLocalFile(mapPath));
    } else {
        // 回退到资源路径
        WebPages->load(QUrl("qrc:/Resources/map.html"));
    }
    connect(WebPages, &QWebEnginePage::loadFinished, this, [this](bool success) {
        if (!success) {
            QMessageBox::warning(this, "错误", "地图加载失败！\n URL:" + WebPages->url().toString());
        }
    });

    shipCounter = 0;
    currentMessageIndex = 0;

    delete shipCounterLabel;

    shipCounterLabel = nullptr;

}

void MapWindow::on_pushButton_LocateMaps_clicked()
{
    aisMessages.clear();
    clearAllMapLabels();
    shipCounter = 0;
    currentMessageIndex = 0;

    updateShipCounterLabel();

    showPlainTextEdit(); // 先显示文本框

    // 重置处理状态
    currentMessageIndex = 0;
    isProcessing = true;

    // 如果之前没有加载成功，尝试重新加载
    if(rawAisMessages.isEmpty()) {
        loadAisMessagesFromResource();
    }

    // 清空文本框
    cipherTextEdit->clear();
    plainTextEdit->clear();

    // 启动定时器
    messageTimer->start(200);

    // 立即处理第一条消息（可选）
    if(!rawAisMessages.isEmpty()) {
        processNextMessage();
    }
}

void MapWindow::on_btnHidePlainTextEdits_clicked()
{
    if (hideOrNot == 0) {
        cipherTextEdit->lower();
        plainTextEdit->lower();
        btnHidePlainTextEdits->setText("显示报文展示框");
        hideOrNot = 1;
    } else {
        cipherTextEdit->raise();
        plainTextEdit->raise();
        btnHidePlainTextEdits->setText("隐藏报文展示框"); // 修复变量名错误
        hideOrNot = 0;
    }
}

void MapWindow::on_btnPauseResume_clicked()
{
    if (isProcessing) {
        messageTimer->stop();
        btnPauseResume->setText("继续接收");
        isProcessing = false;
    } else {
        messageTimer->start(200);
        btnPauseResume->setText("暂停接收");
        isProcessing = true;
    }
}

void MapWindow::processNextMessage() {
    if (currentMessageIndex >= rawAisMessages.size()) {
        messageTimer->stop();
        btnPauseResume->setText("处理完成");
        btnPauseResume->setEnabled(false);
        return;
    }

    // 限制每次处理的消息数量
    int messagesToProcess = qMin(10, rawAisMessages.size() - currentMessageIndex);

    for (int i = 0; i < messagesToProcess; i++) {
        const QPair<QDateTime, QString>& messagePair = rawAisMessages[currentMessageIndex];
        QDateTime timestamp = messagePair.first;
        QString rawMessage = messagePair.second;

        cipherTextEdit->appendPlainText("[" + timestamp.toString("hh:mm:ss") + "] " + rawMessage);

        try {
            AisMessage msg = AisAnal::parseLine(rawMessage, timestamp);
            msg.timestamp = timestamp;

            QString plainText = QString("[%1] MMSI: %2 | 位置: %3, %4 | 航速: %5 节 | 航向: %6°")
                                    .arg(timestamp.toString("hh:mm:ss"))
                                    .arg(msg.mmsi)
                                    .arg(QString::number(msg.latitude, 'f', 6))
                                    .arg(QString::number(msg.longitude, 'f', 6))
                                    .arg(msg.sog)
                                    .arg(msg.cog);

            plainTextEdit->appendPlainText(plainText);
            addAisMessage(msg);

        } catch (const std::exception& e) {
            plainTextEdit->appendPlainText("[" + timestamp.toString("hh:mm:ss") + "] 解析错误: " + QString(e.what()));
        }

        currentMessageIndex++;
    }

    // 批量更新船舶标记，而不是每条消息都更新
    if (currentMessageIndex % 20 == 0 || currentMessageIndex >= rawAisMessages.size()) {
        updateShipMarkers();
    }
}

void MapWindow::handleWebPageMessage(const QJsonObject &message)
{
    QString action = message["action"].toString();
    if (action == "ship_clicked") {
        QString mmsi = message["mmsi"].toString();

        for (const auto& msg : aisMessages) {
            if (msg.mmsi == mmsi) {
                QString info = QString("<b>船舶详细信息</b><br><br>"
                                       "<b>MMSI:</b> %1<br>"
                                       "<b>船名:</b> %2<br>"
                                       "<b>位置:</b> %3°N, %4°E<br>"
                                       "<b>航速:</b> %5 节<br>"
                                       "<b>航向:</b> %6°<br>"
                                       "<b>首向:</b> %7°<br>"
                                       "<b>报文类型:</b> %8<br>"
                                       "<b>最后更新时间:</b> %9<br>"
                                       "<b>原始报文:</b> %10")
                                   .arg(msg.mmsi)
                                   .arg(msg.name.isEmpty() ? "未知" : msg.name)
                                   .arg(msg.latitude, 0, 'f', 6)
                                   .arg(msg.longitude, 0, 'f', 6)
                                   .arg(msg.sog)
                                   .arg(msg.cog)
                                   .arg(msg.heading < 0 ? "未知" : QString::number(msg.heading))
                                   .arg(msg.type)
                                   .arg(msg.timestamp.toString("yyyy-MM-dd hh:mm:ss"))
                                   .arg(msg.rawPayload);

                QMessageBox::information(this, "船舶详细信息", info);
                break;
            }
        }
    }
}

void MapWindow::updateTime()
{
    // 获取当前时间
    QDateTime currentTime = QDateTime::currentDateTime();

    // 将时间格式化为 "HH:mm:ss" 形式
    QString timeString = currentTime.toString("hh:mm:ss");

    // 将时间转换为数字形式
    ui->lcdNumber->display(timeString);
}

void MapWindow::clearAllMapLabels()
{
    // 调用 JavaScript 清理所有标记
    QString js = R"(
        if (typeof clearAllMarkers === 'function') {
            clearAllMarkers();
        } else {
            console.error('clearAllMarkers 函数未定义!');
        }
    )";

    WebPages->runJavaScript(js, [](const QVariant &result) {
        qDebug() << "清理地图标记完成";
    });
}

void MapWindow::resizeEvent(QResizeEvent *event)
{
    QSize newSize = event->size();
    windowHeight = newSize.height();
    windowWidth = newSize.width();
    webMapViewsHeight = windowHeight - 100;
    webMapViewsWidth = windowWidth - 20;

    WebMapViews->setGeometry(10, 80, webMapViewsWidth, webMapViewsHeight);

    if (hideOrNot >= 0) {
        cipherTextEdit->setGeometry(webMapViewsWidth - 300, 90, 300, 300);
        plainTextEdit->setGeometry(webMapViewsWidth - 300, 400, 300, 300);
        btnHidePlainTextEdits->setGeometry(webMapViewsWidth - 90, 10, 100, 50);
        btnPauseResume->setGeometry(webMapViewsWidth - 200, 10, 100, 50);
    }
}
