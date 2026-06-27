#include "mainwindow.h"
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QPushButton>
#include <QFileDialog>
#include <QDir>
#include <QLabel>
#include <QFile>
#include <QTextStream>
#include <QFrame>
#include <QCoreApplication>
#include <QGroupBox>
#include <QMessageBox>
#include <QHeaderView>
#include <QSplitter>
#include <QScrollArea>
#include <QDateTime>
#include <QSettings>
#include <QRegularExpression>
#include <QTextCursor>
#include <QTextCharFormat>
#include <QTabWidget>
#include <QFileInfo>
#include <QDesktopServices>
#include <QProgressBar>

// ================= OpenCV DNN (C++推理用) =================
#include <opencv2/opencv.hpp>
#include <opencv2/dnn.hpp>

// ================= 初始化 =================
MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent), currentIndex(0), currentRotation(0), currentZoomFactor(1.0)
{
    // 初始化数据库
    QString dbPath = QCoreApplication::applicationDirPath() + "/annotation_data.db";
    if (!DatabaseManager::init(dbPath)) {
        dbPath = "annotation_data.db";
        DatabaseManager::init(dbPath);
    }

    setupUI();
    connectSignals();

    scene = new QGraphicsScene(this);
    view->setScene(scene);

    process = new QProcess(this);
    connect(process, &QProcess::readyReadStandardOutput, this, &MainWindow::readOutput);
    connect(process, &QProcess::readyReadStandardError, this, &MainWindow::readOutput);
    connect(process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this](int exitCode, QProcess::ExitStatus status) {
                switch (activeTask) {
                case Training:
                    onTrainingFinished(exitCode, status);
                    break;
                case Quantization:
                    onQuantizationFinished(exitCode, status);
                    break;
                case PythonCheck:
                    onPythonCheckFinished(exitCode, status);
                    break;
                default:
                    finished();
                    break;
                }
                activeTask = None;
            });

    // 默认类别（表情识别7类）— 顺序与JAFFE数据集一致
    classNames << "angry" << "disgust" << "fear" << "happy" << "sad" << "surprise" << "neutral";
    labelInput->setText("0:angry,1:disgust,2:fear,3:happy,4:sad,5:surprise,6:neutral");
    updateLabels();

    setWindowTitle("工程基础与创新设计B - 智能图像标注与推理系统");
    resize(1500, 950);

    statusBar->showMessage("就绪 - 请打开图片文件夹开始工作", 3000);

    // 自动检测Python环境
    checkPythonEnvironment();
}

MainWindow::~MainWindow()
{
}

// ================= Python环境检测 =================
void MainWindow::checkPythonEnvironment()
{
    pythonReady = findPythonWithUltralytics();

    if (pythonReady) {
        pythonStatusLabel->setText("✅ Python: " + pythonExePath);
        pythonStatusLabel->setStyleSheet("color: #2E7D32; font-size: 11px; padding: 2px;");
        appendSuccessLog(QString("Python环境检测通过: %1").arg(pythonExePath));
    } else {
        pythonStatusLabel->setText("❌ Python未就绪");
        pythonStatusLabel->setStyleSheet("color: #D32F2F; font-size: 11px; padding: 2px; background: #FFEBEE; border-radius: 3px;");

        QStringList candidatePaths = {
            "/opt/anaconda3/bin/python3",
            "/opt/anaconda3/envs/zyx/bin/python",
            "/Users/ddd/anaconda3/bin/python3",
            "/Users/ddd/anaconda3/envs/zyx/bin/python",
            "/usr/local/bin/python3",
            "python3"
        };

        for (const QString &path : candidatePaths) {
            if (QFile::exists(path) || path == "python3") {
                QProcess *checkProc = new QProcess(this);
                checkProc->start(path, QStringList() << "-c" << "import ultralytics; print('OK')");
                if (checkProc->waitForFinished(5000)) {
                    QString output = QString::fromUtf8(checkProc->readAllStandardOutput()).trimmed();
                    if (output == "OK") {
                        pythonExePath = path;
                        pythonReady = true;
                        pythonStatusLabel->setText("✅ Python: " + pythonExePath);
                        pythonStatusLabel->setStyleSheet("color: #2E7D32; font-size: 11px; padding: 2px;");
                        appendSuccessLog(QString("Python环境检测通过: %1").arg(pythonExePath));
                        checkProc->deleteLater();
                        return;
                    }
                    QString errOutput = QString::fromUtf8(checkProc->readAllStandardError()).trimmed();
                    if (errOutput.contains("ModuleNotFoundError")) {
                        QRegularExpression modRe("No module named '([^']+)'");
                        QRegularExpressionMatch match = modRe.match(errOutput);
                        if (match.hasMatch()) {
                            pythonMissingModules.append(match.captured(1));
                        }
                    }
                }
                checkProc->deleteLater();
            }
        }

        appendErrorLog("Python环境检测失败！缺失模块: " + pythonMissingModules.join(", "));
        appendErrorLog("请安装: pip install ultralytics onnxruntime opencv-python");
        appendLog("提示: conda activate zyx 后 pip install ultralytics");
    }
}

bool MainWindow::findPythonWithUltralytics()
{
    QStringList candidatePaths = {
        "/opt/anaconda3/envs/zyx/bin/python",
        "/Users/ddd/anaconda3/envs/zyx/bin/python",
        "/opt/anaconda3/bin/python3",
        "/usr/local/bin/python3"
    };

    for (const QString &path : candidatePaths) {
        if (!QFile::exists(path)) continue;

        QProcess checkProc;
        checkProc.start(path, QStringList() << "-c" << "import ultralytics; print('OK')");
        if (checkProc.waitForFinished(5000)) {
            if (QString::fromUtf8(checkProc.readAllStandardOutput()).trimmed() == "OK") {
                pythonExePath = path;
                return true;
            }
        }
    }
    return false;
}

// ================= 路径管理工具函数 =================
QString MainWindow::getScriptPath(const QString &scriptName)
{
    QString srcDir = "/Users/ddd/Documents/课程作业/工程基础与创新设计/zyx/ke2/";
    if (QFile::exists(srcDir + scriptName))
        return srcDir + scriptName;
    QString appDir = QCoreApplication::applicationDirPath() + "/";
    return appDir + scriptName;
}

QString MainWindow::getModelPath(const QString &modelName)
{
    QStringList candidates = {
        "/Users/ddd/Documents/课程作业/工程基础与创新设计/zyx/models/" + modelName,
        QCoreApplication::applicationDirPath() + "/models/" + modelName,
        "models/" + modelName,
        modelName
    };
    for (const auto &p : candidates) {
        if (QFile::exists(p)) return p;
    }
    return candidates[0];
}

// ================= 日志辅助函数 =================
void MainWindow::appendLog(const QString &text, const QColor &color)
{
    if (color.isValid()) {
        QTextCharFormat fmt;
        fmt.setForeground(color);
        QTextCursor cursor = logOutput->textCursor();
        cursor.movePosition(QTextCursor::End);
        cursor.insertText(text + "\n", fmt);
        logOutput->setTextCursor(cursor);
    } else {
        logOutput->append(text);
    }
    logOutput->ensureCursorVisible();
}

void MainWindow::appendErrorLog(const QString &text)
{
    appendLog("❌ " + text, QColor("#D32F2F"));
}

void MainWindow::appendSuccessLog(const QString &text)
{
    appendLog("✅ " + text, QColor("#2E7D32"));
}

// ================================================================
//   UI 布局（双 Tab 设计）
//
//   顶层 QTabWidget（占满中央区域，底部固定日志栏）
//   ┌─────────────────────────────────────────────────────┐
//   │  [📷 图片标注]  [🎯 训练 / 量化 / 推理]              │ ← 顶层 Tab
//   ├─────────────────────────────────────────────────────┤
//   │                                                     │
//   │  Tab1: 标注页                                        │
//   │  ┌──────────┬────────────────────────────────────┐  │
//   │  │ 图片列表  │  [标注工具栏]                       │  │
//   │  │          │  ┌──────────────────────────────┐  │  │
//   │  │ 1. ✅    │  │       图片 + 框标注视图        │  │  │
//   │  │ 2. ⬜    │  └──────────────────────────────┘  │  │
//   │  │ [◀][▶]  │  [视图控制栏]                       │  │
//   │  └──────────┴────────────────────────────────────┘  │
//   │                                                     │
//   │  Tab2: 训练/量化/推理页                              │
//   │  ┌─────────────────────────────────────────────┐   │
//   │  │  Python状态栏                                │   │
//   │  │  [🎯训练] [🔧量化] [🔍推理]  ← 子 Tab        │   │
//   │  └─────────────────────────────────────────────┘   │
//   ├─────────────────────────────────────────────────────┤
//   │ ▼ 实时日志输出 (可折叠)                              │
//   └─────────────────────────────────────────────────────┘
// ================================================================

void MainWindow::setupUI()
{
    QWidget *central = new QWidget;
    setCentralWidget(central);

    // ================================================================
    // ========== Tab 1: 图片标注页 ==========
    // ================================================================

    // ===== 左侧：图片列表 + 导航 =====
    QWidget *leftPanel = new QWidget;
    QVBoxLayout *leftLayout = new QVBoxLayout(leftPanel);
    leftLayout->setContentsMargins(4, 4, 4, 4);
    leftLayout->setSpacing(4);

    QLabel *listTitle = new QLabel("📷 图片列表");
    listTitle->setStyleSheet("font-weight:bold; font-size:12px; color:#333;");
    leftLayout->addWidget(listTitle);

    imageListWidget = new QListWidget();
    imageListWidget->setToolTip("单击切换图片 | ✅=已标注 ⬜=未标注");
    leftLayout->addWidget(imageListWidget, 1);

    QHBoxLayout *navLayout = new QHBoxLayout;
    QPushButton *btnPrev = new QPushButton("◀ 上一张");
    QPushButton *btnNext = new QPushButton("下一张 ▶");
    btnPrev->setMinimumHeight(28);
    btnNext->setMinimumHeight(28);
    navLayout->addWidget(btnPrev);
    navLayout->addWidget(btnNext);
    leftLayout->addLayout(navLayout);

    QPushButton *btnDatasetInfo = new QPushButton("📊 数据集概览");
    btnDatasetInfo->setMinimumHeight(24);
    btnDatasetInfo->setStyleSheet("font-size:10px; padding:2px;");
    leftLayout->addWidget(btnDatasetInfo);

    QLabel *statsLabel = new QLabel("共 0 张");
    statsLabel->setAlignment(Qt::AlignCenter);
    statsLabel->setStyleSheet("font-size:10px; color:#888;");
    listStatsLabel = statsLabel;
    leftLayout->addWidget(statsLabel);

    leftPanel->setMaximumWidth(200);
    leftPanel->setMinimumWidth(150);

    // ===== 右侧：标注视图区 =====
    QWidget *annoViewPanel = new QWidget;
    QVBoxLayout *annoViewLayout = new QVBoxLayout(annoViewPanel);
    annoViewLayout->setContentsMargins(4, 4, 4, 4);
    annoViewLayout->setSpacing(6);

    // --- 标注工具栏（GroupBox 包裹，视觉更清晰）---
    QGroupBox *annoGroup = new QGroupBox("标注工具");
    annoGroup->setStyleSheet("QGroupBox { font-weight:bold; font-size:12px; padding-top:6px; }");
    QHBoxLayout *annoToolbar = new QHBoxLayout(annoGroup);
    annoToolbar->setSpacing(6);

    QLabel *labelTitle = new QLabel("类别定义:");
    labelTitle->setStyleSheet("font-size:11px;");
    annoToolbar->addWidget(labelTitle);

    labelInput = new QLineEdit();
    labelInput->setPlaceholderText("如: 0:angry,1:disgust,2:fear,3:happy,4:sad,5:surprise,6:neutral");
    annoToolbar->addWidget(labelInput, 3);

    QLabel *curLabel = new QLabel("当前类别:");
    curLabel->setStyleSheet("font-size:11px;");
    annoToolbar->addWidget(curLabel);

    labelBox = new QComboBox();
    labelBox->setMinimumWidth(110);
    annoToolbar->addWidget(labelBox, 1);

    btnDeleteBox = new QPushButton("🗑 删除框");
    QPushButton *btnChangeClass = new QPushButton("✏ 改类别");
    btnDeleteBox->setMinimumHeight(26);
    btnChangeClass->setMinimumHeight(26);
    annoToolbar->addWidget(btnDeleteBox);
    annoToolbar->addWidget(btnChangeClass);

    QFrame *sepLine = new QFrame();
    sepLine->setFrameShape(QFrame::VLine);
    sepLine->setFrameShadow(QFrame::Sunken);
    annoToolbar->addWidget(sepLine);

    btnUndo = new QPushButton("↩ 撤销");
    btnRedo = new QPushButton("↪ 重做");
    btnUndo->setMinimumHeight(26);
    btnRedo->setMinimumHeight(26);
    btnUndo->setEnabled(false);
    btnRedo->setEnabled(false);
    annoToolbar->addWidget(btnUndo);
    annoToolbar->addWidget(btnRedo);

    annoViewLayout->addWidget(annoGroup);

    // --- 图片视图 ---
    view = new AnnotationView();
    view->setMinimumSize(400, 350);
    annoViewLayout->addWidget(view, 1);

    // --- 视图控制栏 ---
    QGroupBox *viewGroup = new QGroupBox("视图控制");
    viewGroup->setStyleSheet("QGroupBox { font-weight:bold; font-size:11px; padding-top:4px; }");
    QHBoxLayout *viewCtrlBar = new QHBoxLayout(viewGroup);
    viewCtrlBar->setSpacing(4);

    QPushButton *btnZoomIn  = new QPushButton("🔍+");
    QPushButton *btnZoomOut = new QPushButton("🔍−");
    QPushButton *btnZoomFit = new QPushButton("适应");
    QPushButton *btnResetView = new QPushButton("复位");
    QPushButton *btnRotateL = new QPushButton("↺ 左转");
    QPushButton *btnRotateR = new QPushButton("右转 ↻");

    QString viewBtnStyle = "QPushButton { padding:4px 10px; font-size:11px; border:1px solid #ccc; border-radius:3px; }"
                           "QPushButton:hover { background:#E3F2FD; border-color:#1976D2; }";
    for (auto *b : {btnZoomIn, btnZoomOut, btnZoomFit, btnResetView, btnRotateL, btnRotateR})
        b->setStyleSheet(viewBtnStyle);

    viewCtrlBar->addWidget(btnZoomIn);
    viewCtrlBar->addWidget(btnZoomOut);
    viewCtrlBar->addWidget(btnZoomFit);
    viewCtrlBar->addWidget(btnResetView);
    viewCtrlBar->addStretch();
    viewCtrlBar->addWidget(btnRotateL);
    viewCtrlBar->addWidget(btnRotateR);
    annoViewLayout->addWidget(viewGroup);

    // ===== 标注页主体：左列表 + 右视图 =====
    mainSplitter = new QSplitter(Qt::Horizontal);
    mainSplitter->setHandleWidth(2);
    mainSplitter->addWidget(leftPanel);
    mainSplitter->addWidget(annoViewPanel);
    mainSplitter->setStretchFactor(0, 0);
    mainSplitter->setStretchFactor(1, 1);

    QWidget *annotationPage = new QWidget;
    QVBoxLayout *annotationPageLayout = new QVBoxLayout(annotationPage);
    annotationPageLayout->setContentsMargins(0, 0, 0, 0);
    annotationPageLayout->addWidget(mainSplitter);

    // ================================================================
    // ========== Tab 2: 训练 / 量化 / 推理页 ==========
    // ================================================================

    // Python状态标签（顶部）
    pythonStatusLabel = new QLabel("⏳ 检测Python...");
    pythonStatusLabel->setStyleSheet("font-size:11px; padding:4px 8px; background:#FFF3E0; border-radius:4px;");
    pythonStatusLabel->setWordWrap(true);

    // ---- 子 Tab：训练 ----
    QWidget *trainTab = new QWidget;
    QVBoxLayout *trainLayout = new QVBoxLayout(trainTab);
    trainLayout->setContentsMargins(12, 12, 12, 12);
    trainLayout->setSpacing(10);

    QGroupBox *trainCfgGroup = new QGroupBox("训练配置");
    trainCfgGroup->setStyleSheet("QGroupBox { font-weight:bold; font-size:12px; padding-top:8px; }");
    QVBoxLayout *trainCfgLayout = new QVBoxLayout(trainCfgGroup);
    trainCfgLayout->setSpacing(8);

    trainCfgLayout->addWidget(new QLabel("📂 数据集路径:"));
    datasetEdit = new QLineEdit();
    datasetEdit->setPlaceholderText("选择数据集目录...");
    QHBoxLayout *dsRow = new QHBoxLayout;
    dsRow->addWidget(datasetEdit);
    QPushButton *btnDataset = new QPushButton("浏览...");
    dsRow->addWidget(btnDataset);
    trainCfgLayout->addLayout(dsRow);

    QHBoxLayout *paramRow = new QHBoxLayout;
    paramRow->addWidget(new QLabel("Epochs:"));
    epochSpin = new QSpinBox();
    epochSpin->setRange(1, 500);
    epochSpin->setValue(30);
    paramRow->addWidget(epochSpin);
    paramRow->addSpacing(12);
    paramRow->addWidget(new QLabel("Batch:"));
    batchSpin = new QSpinBox();
    batchSpin->setRange(1, 128);
    batchSpin->setValue(16);
    paramRow->addWidget(batchSpin);
    paramRow->addStretch();
    trainCfgLayout->addLayout(paramRow);

    // 训练集/验证集比例
    QHBoxLayout *splitRow = new QHBoxLayout;
    splitRow->addWidget(new QLabel("训练集比例:"));
    splitSlider = new QSlider(Qt::Horizontal);
    splitSlider->setRange(50, 95);
    splitSlider->setValue(80);
    splitSlider->setTickPosition(QSlider::TicksBelow);
    splitSlider->setTickInterval(5);
    splitRow->addWidget(splitSlider, 1);
    splitLabel = new QLabel("80%");
    splitLabel->setMinimumWidth(40);
    splitLabel->setStyleSheet("font-weight:bold; font-size:13px; color:#1976D2;");
    splitRow->addWidget(splitLabel);
    QLabel *valLabel = new QLabel("验证集 20%");
    valLabel->setStyleSheet("font-size:11px; color:#888;");
    splitRow->addWidget(valLabel);
    trainCfgLayout->addLayout(splitRow);

    // 自动转ONNX选项
    autoOnnxCheck = new QCheckBox("训练完成后自动转换为 ONNX 格式");
    autoOnnxCheck->setChecked(true);
    autoOnnxCheck->setStyleSheet("font-size:11px; color:#555;");
    trainCfgLayout->addWidget(autoOnnxCheck);

    trainLayout->addWidget(trainCfgGroup);

    QGroupBox *trainRunGroup = new QGroupBox("训练进度");
    trainRunGroup->setStyleSheet("QGroupBox { font-weight:bold; font-size:12px; padding-top:8px; }");
    QVBoxLayout *trainRunLayout = new QVBoxLayout(trainRunGroup);

    trainStatusLabel = new QLabel("状态: 就绪");
    trainStatusLabel->setStyleSheet("font-size:12px; color:#555;");
    trainProgressBar = new QProgressBar();
    trainProgressBar->setRange(0, 100);
    trainProgressBar->setValue(0);
    trainProgressBar->setTextVisible(true);
    trainProgressBar->setFormat("%v%");
    trainProgressBar->setMinimumHeight(18);
    trainRunLayout->addWidget(trainStatusLabel);
    trainRunLayout->addWidget(trainProgressBar);

    trainLayout->addWidget(trainRunGroup);

    QPushButton *btnTrain = new QPushButton("▶  开始 YOLOv8 训练");
    btnTrain->setMinimumHeight(40);
    btnTrain->setStyleSheet(
        "QPushButton { font-weight:bold; padding:10px; background:#1976D2; "
        "color:white; border:none; border-radius:6px; font-size:13px; }"
        "QPushButton:hover { background:#1E88E5; }"
        "QPushButton:pressed { background:#1565C0; }"
    );
    trainLayout->addWidget(btnTrain);
    trainLayout->addStretch();

    // ---- 子 Tab：量化 ----
    QWidget *quantTab = new QWidget;
    QVBoxLayout *quantLayout = new QVBoxLayout(quantTab);
    quantLayout->setContentsMargins(12, 12, 12, 12);
    quantLayout->setSpacing(10);

    QGroupBox *quantCfgGroup = new QGroupBox("量化配置");
    quantCfgGroup->setStyleSheet("QGroupBox { font-weight:bold; font-size:12px; padding-top:8px; }");
    QVBoxLayout *quantCfgLayout = new QVBoxLayout(quantCfgGroup);
    quantCfgLayout->setSpacing(8);

    quantCfgLayout->addWidget(new QLabel("量化类型:"));
    quantTypeCombo = new QComboBox();
    quantTypeCombo->addItem("FP16 半精度 (推荐, 体积减半)");
    quantTypeCombo->addItem("INT8 权重量化 (精度降低, 体积不变)");
    quantCfgLayout->addWidget(quantTypeCombo);

    quantCfgLayout->addWidget(new QLabel("输入模型 (.pt / .onnx):"));
    onnxInputEdit = new QLineEdit();
    onnxInputEdit->setPlaceholderText("选择 .pt 或 .onnx 模型...");
    QPushButton *btnSelectOnnxInput = new QPushButton("浏览...");
    QHBoxLayout *onnxRow = new QHBoxLayout;
    onnxRow->addWidget(onnxInputEdit);
    onnxRow->addWidget(btnSelectOnnxInput);
    quantCfgLayout->addLayout(onnxRow);

    quantLayout->addWidget(quantCfgGroup);

    QGroupBox *quantRunGroup = new QGroupBox("量化进度");
    quantRunGroup->setStyleSheet("QGroupBox { font-weight:bold; font-size:12px; padding-top:8px; }");
    QVBoxLayout *quantRunLayout = new QVBoxLayout(quantRunGroup);

    quantStatusLabel = new QLabel("状态: 未执行");
    quantStatusLabel->setStyleSheet("font-size:12px; color:#555;");
    quantProgressBar = new QProgressBar();
    quantProgressBar->setRange(0, 100);
    quantProgressBar->setValue(0);
    quantProgressBar->setMinimumHeight(18);
    quantRunLayout->addWidget(quantStatusLabel);
    quantRunLayout->addWidget(quantProgressBar);

    quantLayout->addWidget(quantRunGroup);

    QPushButton *btnQuantize = new QPushButton("🔄  开始量化");
    btnQuantize->setMinimumHeight(40);
    btnQuantize->setStyleSheet(
        "QPushButton { font-weight:bold; padding:8px; background:#7B1FA2; "
        "color:white; border:none; border-radius:6px; font-size:12px; }"
        "QPushButton:hover { background:#8E24AA; }"
    );
    quantLayout->addWidget(btnQuantize);
    quantLayout->addStretch();

    // ---- 子 Tab：推理 ----
    QWidget *inferTab = new QWidget;
    QVBoxLayout *inferLayout = new QVBoxLayout(inferTab);
    inferLayout->setContentsMargins(12, 12, 12, 12);
    inferLayout->setSpacing(10);

    QGroupBox *inferCfgGroup = new QGroupBox("推理配置");
    inferCfgGroup->setStyleSheet("QGroupBox { font-weight:bold; font-size:12px; padding-top:8px; }");
    QVBoxLayout *inferCfgLayout = new QVBoxLayout(inferCfgGroup);
    inferCfgLayout->setSpacing(8);

    inferCfgLayout->addWidget(new QLabel("🖼️ 待推理图片:"));
    inferenceImageEdit = new QLineEdit();
    inferenceImageEdit->setPlaceholderText("选择一张图片进行表情识别...");
    btnSelectInferenceImage = new QPushButton("浏览...");
    QHBoxLayout *imgRow = new QHBoxLayout;
    imgRow->addWidget(inferenceImageEdit);
    imgRow->addWidget(btnSelectInferenceImage);
    inferCfgLayout->addLayout(imgRow);

    inferCfgLayout->addWidget(new QLabel("🧠 推理模型 (.pt / .onnx):"));
    inferenceModelEdit = new QLineEdit();
    inferenceModelEdit->setPlaceholderText("选择推理模型（支持 .pt / .onnx）...");
    QPushButton *btnSelectInferenceModel = new QPushButton("浏览...");
    QHBoxLayout *modelRow = new QHBoxLayout;
    modelRow->addWidget(inferenceModelEdit);
    modelRow->addWidget(btnSelectInferenceModel);
    inferCfgLayout->addLayout(modelRow);

    inferCfgLayout->addWidget(new QLabel("推理引擎:"));
    inferenceModeCombo = new QComboBox();
    inferenceModeCombo->addItem("C++ ONNX Runtime (推荐)", "cpp");
    inferenceModeCombo->addItem("Python ONNX Runtime", "python");
    inferCfgLayout->addWidget(inferenceModeCombo);

    inferLayout->addWidget(inferCfgGroup);

    inferenceResultLabel = new QLabel("结果: 待检测");
    inferenceResultLabel->setStyleSheet(
        "font-weight:bold; font-size:16px; padding:12px; border:2px solid #BBDEFB; "
        "border-radius:8px; background:#E3F2FD; color:#0D47A1;"
    );
    inferenceResultLabel->setAlignment(Qt::AlignCenter);
    inferenceResultLabel->setMinimumHeight(60);
    inferLayout->addWidget(inferenceResultLabel);

    QPushButton *btnInference = new QPushButton("🚀  执行推理");
    btnInference->setMinimumHeight(40);
    btnInference->setStyleSheet(
        "QPushButton { font-weight:bold; padding:10px; background:#388E3C; "
        "color:white; border:none; border-radius:6px; font-size:13px; }"
        "QPushButton:hover { background:#43A047; }"
    );
    inferLayout->addWidget(btnInference);
    inferLayout->addStretch();

    // ---- 组装训练/量化/推理子 Tab ----
    QTabWidget *subTabs = new QTabWidget();
    subTabs->setDocumentMode(true);
    subTabs->addTab(trainTab, "🎯 训练");
    subTabs->addTab(quantTab, "🔧 量化");
    subTabs->addTab(inferTab, "🔍 推理");

    // 训练/量化/推理整页（Python状态 + 子Tab）
    QWidget *modelPage = new QWidget;
    QVBoxLayout *modelPageLayout = new QVBoxLayout(modelPage);
    modelPageLayout->setContentsMargins(6, 6, 6, 6);
    modelPageLayout->setSpacing(4);
    modelPageLayout->addWidget(pythonStatusLabel);
    modelPageLayout->addWidget(subTabs, 1);

    // ================================================================
    // ========== 顶层 Tab ==========
    // ================================================================
    QTabWidget *mainTabs = new QTabWidget();
    mainTabs->setDocumentMode(false);
    mainTabs->setStyleSheet(
        "QTabWidget::pane { border:1px solid #CCCCCC; }"
        "QTabBar::tab { padding:6px 20px; font-size:13px; font-weight:bold; }"
        "QTabBar::tab:selected { background:#1976D2; color:white; border-radius:4px 4px 0 0; }"
        "QTabBar::tab:!selected { background:#F5F5F5; color:#555; }"
        "QTabBar::tab:hover:!selected { background:#E3F2FD; }"
    );
    mainTabs->addTab(annotationPage, "📷  图片标注");
    mainTabs->addTab(modelPage,      "🎯  训练 / 量化 / 推理");

    // ========== 底部日志区（固定，不属于 Tab）==========
    QWidget *logContainer = new QWidget;
    QVBoxLayout *logContainerLayout = new QVBoxLayout(logContainer);
    logContainerLayout->setContentsMargins(4, 0, 4, 4);
    logContainerLayout->setSpacing(2);

    QHBoxLayout *logHeader = new QHBoxLayout;
    QLabel *logTitle = new QLabel("📋 实时日志");
    logTitle->setStyleSheet("font-weight:bold; font-size:11px;");
    logHeader->addWidget(logTitle);
    logHeader->addStretch();

    QPushButton *btnToggleLog = new QPushButton("收起 ▲");
    btnToggleLog->setToolTip("折叠/展开日志区域");
    btnToggleLog->setMaximumSize(64, 22);
    btnToggleLog->setStyleSheet("font-size:10px; padding:1px 6px;");
    logHeader->addWidget(btnToggleLog);
    logContainerLayout->addLayout(logHeader);

    logOutput = new QTextEdit();
    logOutput->setReadOnly(true);
    logOutput->setFont(QFont("Menlo", 10));
    logOutput->setMinimumHeight(60);  // 最小高度
    // 不设置 MaximumHeight，让它可以随窗口调整大小
    logOutput->setStyleSheet(
        "QTextEdit { background:#1E1E1E; color:#D4D4D4; border:1px solid #444; "
        "border-radius:4px; padding:4px; font-family:'Menlo','Consolas',monospace; }"
    );
    logContainerLayout->addWidget(logOutput, 1);  // stretch factor = 1，可调整

    QHBoxLayout *logBtnRow = new QHBoxLayout;
    QPushButton *btnClearLog        = new QPushButton("清空日志");
    QPushButton *btnShowTrainHistory = new QPushButton("📊 训练记录");
    QPushButton *btnShowInferHistory = new QPushButton("📊 推理记录");
    btnClearLog->setMaximumHeight(24);
    btnShowTrainHistory->setMaximumHeight(24);
    btnShowInferHistory->setMaximumHeight(24);
    logBtnRow->addWidget(btnClearLog);
    logBtnRow->addWidget(btnShowTrainHistory);
    logBtnRow->addWidget(btnShowInferHistory);
    logBtnRow->addStretch();
    logContainerLayout->addLayout(logBtnRow);

    logOutput->setVisible(true);
    connect(btnToggleLog, &QPushButton::clicked, [this, btnToggleLog]() {
        bool visible = logOutput->isVisible();
        logOutput->setVisible(!visible);
        btnToggleLog->setText(visible ? "展开 ▼" : "收起 ▲");
    });

    // ========== 整体主布局 ==========
    QVBoxLayout *mainLayout = new QVBoxLayout(central);
    mainLayout->setContentsMargins(4, 4, 4, 4);
    mainLayout->setSpacing(4);
    mainLayout->addWidget(mainTabs, 1);
    mainLayout->addWidget(logContainer);

    // ================================================================
    // ===== 信号绑定 =====
    // ================================================================

    // 导航
    connect(btnPrev, &QPushButton::clicked, this, &MainWindow::prevImage);
    connect(btnNext, &QPushButton::clicked, this, &MainWindow::nextImage);
    connect(btnDatasetInfo, &QPushButton::clicked, this, &MainWindow::showDatasetInfo);
    connect(imageListWidget, &QListWidget::currentRowChanged, this, &MainWindow::onImageSelected);
    connect(imageListWidget, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem *item) {
        int row = imageListWidget->row(item);
        if (row >= 0 && row != currentIndex) {
            saveLabels();
            currentIndex = row;
            loadImage();
        }
    });

    // 标注
    connect(btnDeleteBox, &QPushButton::clicked, this, &MainWindow::deleteSelectedBox);
    connect(btnChangeClass, &QPushButton::clicked, this, &MainWindow::changeBoxClass);
    connect(btnUndo, &QPushButton::clicked, this, &MainWindow::undoAnnotation);
    connect(btnRedo, &QPushButton::clicked, this, &MainWindow::redoAnnotation);
    connect(labelInput, &QLineEdit::editingFinished, this, &MainWindow::updateLabels);
    connect(labelBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int idx) {
        if (view->selectedBoxIndex() >= 0 && idx >= 0) {
            QColor c = QColor((idx * 37) % 200 + 55, (idx * 73) % 200 + 55, (idx * 113) % 200 + 55);
            view->setBoxClass(view->selectedBoxIndex(), idx, c);
        }
    });

    // 视图控制
    connect(btnZoomIn,    &QPushButton::clicked, view, &AnnotationView::zoomIn);
    connect(btnZoomOut,   &QPushButton::clicked, view, &AnnotationView::zoomOut);
    connect(btnZoomFit,   &QPushButton::clicked, view, &AnnotationView::zoomFit);
    connect(btnResetView, &QPushButton::clicked, view, &AnnotationView::resetView);
    connect(btnRotateL,   &QPushButton::clicked, this, &MainWindow::rotateLeft);
    connect(btnRotateR,   &QPushButton::clicked, this, &MainWindow::rotateRight);

    // 训练
    connect(btnDataset, &QPushButton::clicked, this, &MainWindow::selectDatasetDir);
    connect(btnTrain,   &QPushButton::clicked, this, &MainWindow::startTraining);
    connect(splitSlider, &QSlider::valueChanged, this, [this](int val) {
        splitLabel->setText(QString("%1%").arg(val));
    });

    // 量化
    connect(btnSelectOnnxInput, &QPushButton::clicked, this, &MainWindow::selectOnnxModelForQuantize);
    connect(btnQuantize,        &QPushButton::clicked, this, &MainWindow::startQuantization);

    // 推理
    connect(btnSelectInferenceModel, &QPushButton::clicked, this, &MainWindow::selectInferenceModel);
    connect(btnSelectInferenceImage, &QPushButton::clicked, this, &MainWindow::selectInferenceImages);
    connect(btnInference, &QPushButton::clicked, [this]() {
        if (inferenceModeCombo->currentData().toString() == "cpp")
            runInferenceCpp();
        else
            runInferencePython();
    });

    // 日志
    connect(btnClearLog,         &QPushButton::clicked, [this]() { logOutput->clear(); });
    connect(btnShowTrainHistory, &QPushButton::clicked, this, &MainWindow::showTrainingHistory);
    connect(btnShowInferHistory, &QPushButton::clicked, this, &MainWindow::showInferenceHistory);

    // AnnotationView信号
    connect(view, &AnnotationView::boxSelected, this, [this](int idx) {
        if (idx >= 0 && idx < view->boxInfos.size()) {
            labelBox->setCurrentIndex(view->boxInfos[idx].classId);
            appendLog(QString("[选中] 第%1个框 → 类别: %2")
                      .arg(idx+1).arg(classNames.value(view->boxInfos[idx].classId)));
        }
    });
    connect(view, &AnnotationView::boxChanged, this, &MainWindow::updateUndoRedoState);
}

// ================= 菜单栏 / 工具栏 / 状态栏 =================
void MainWindow::setupMenuBar()
{
    menuBar = new QMenuBar(this);
    setMenuBar(menuBar);

    QMenu *fileMenu = menuBar->addMenu("文件(&F)");

    QAction *actOpenFolder = new QAction("打开图片文件夹", this);
    actOpenFolder->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_O));
    connect(actOpenFolder, &QAction::triggered, this, &MainWindow::loadFolder);
    fileMenu->addAction(actOpenFolder);

    fileMenu->addSeparator();

    QAction *actExit = new QAction("退出", this);
    actExit->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_Q));
    connect(actExit, &QAction::triggered, this, &QWidget::close);
    fileMenu->addAction(actExit);

    QMenu *editMenu = menuBar->addMenu("编辑(&E)");
    QAction *actDelete = new QAction("删除选中标注框", this);
    actDelete->setShortcut(QKeySequence::Delete);
    connect(actDelete, &QAction::triggered, this, &MainWindow::deleteSelectedBox);
    editMenu->addAction(actDelete);

    QMenu *viewMenu = menuBar->addMenu("视图(&V)");
    QAction *actZoomIn = new QAction("放大", this);
    actZoomIn->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_Equal));
    connect(actZoomIn, &QAction::triggered, this, &MainWindow::zoomIn);
    viewMenu->addAction(actZoomIn);

    QAction *actZoomOut = new QAction("缩小", this);
    actZoomOut->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_Minus));
    connect(actZoomOut, &QAction::triggered, this, &MainWindow::zoomOut);
    viewMenu->addAction(actZoomOut);

    QAction *actZoomFit = new QAction("适应窗口", this);
    actZoomFit->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_0));
    connect(actZoomFit, &QAction::triggered, this, &MainWindow::zoomFit);
    viewMenu->addAction(actZoomFit);

    QMenu *helpMenu = menuBar->addMenu("帮助(&H)");
    QAction *actAbout = new QAction("关于", this);
    connect(actAbout, &QAction::triggered, [this]() {
        QMessageBox::about(this, "关于",
            "<h3>智能图像标注与推理系统</h3>"
            "<p>工程基础与创新设计B 课程项目</p>"
            "<p>基于 Qt6 + C++ / OpenCV / YOLOv8 / ONNX</p>"
            "<p>功能：图像标注、YOLO训练、模型量化、表情分类推理</p>");
    });
    helpMenu->addAction(actAbout);

    QAction *actUsage = new QAction("使用说明", this);
    connect(actUsage, &QAction::triggered, [this]() {
        QMessageBox::information(this, "使用说明",
            "<h3>操作流程</h3>"
            "<p><b>1. 标注</b>: 打开文件夹 → 拖拽画框 → 选类别 → 自动保存</p>"
            "<p><b>2. 训练</b>: 设置数据集路径 → 设Epoch/Batch → 点训练</p>"
            "<p><b>3. 量化</b>: 切换到「量化」Tab → 选ONNX模型 → 执行量化</p>"
            "<p><b>4. 推理</b>: 切换到「推理」Tab → 选模型 → 加载图片 → 执行推理</p>"
            "<hr>"
            "<p><b>快捷键</b>: Ctrl+滚轮缩放 | Ctrl+拖拽平移 | Delete删框</p>"
            "<p><b>依赖</b>: 需安装ultralytics, onnxruntime, opencv-python</p>");
    });
    helpMenu->addAction(actUsage);
}

void MainWindow::setupToolBar()
{
    toolBar = addToolBar("主工具栏");
    toolBar->setMovable(false);

    QAction *actOpen = toolBar->addAction("📂 打开文件夹");
    connect(actOpen, &QAction::triggered, this, &MainWindow::loadFolder);

    QAction *actOpenImg = toolBar->addAction("🖼 打开图片");
    connect(actOpenImg, &QAction::triggered, this, &MainWindow::openSingleImage);
    toolBar->addSeparator();

    QAction *actPrev = toolBar->addAction("◀ 上张");
    connect(actPrev, &QAction::triggered, this, &MainWindow::prevImage);
    QAction *actNext = toolBar->addAction("下张 ▶");
    connect(actNext, &QAction::triggered, this, &MainWindow::nextImage);
    toolBar->addSeparator();

    QAction *actZoomIn = toolBar->addAction("🔍+ 放大");
    connect(actZoomIn, &QAction::triggered, this, &MainWindow::zoomIn);
    QAction *actZoomOut = toolBar->addAction("- 缩小");
    connect(actZoomOut, &QAction::triggered, this, &MainWindow::zoomOut);
    toolBar->addSeparator();

    QAction *actDelBox = toolBar->addAction("❌ 删除框");
    connect(actDelBox, &QAction::triggered, this, &MainWindow::deleteSelectedBox);

    QAction *actUndo = toolBar->addAction("↩ 撤销");
    connect(actUndo, &QAction::triggered, this, &MainWindow::undoAnnotation);
    QAction *actRedo = toolBar->addAction("↪ 重做");
    connect(actRedo, &QAction::triggered, this, &MainWindow::redoAnnotation);
}

void MainWindow::setupStatusBar()
{
    statusBar = new QStatusBar(this);
    setStatusBar(statusBar);
    statusBar->showMessage("就绪");
}

void MainWindow::connectSignals()
{
    setupMenuBar();
    setupToolBar();
    setupStatusBar();
}

void MainWindow::exitApp()
{
    close();
}

// ================= 文件加载 =================
void MainWindow::loadFolder()
{
    folderPath = QFileDialog::getExistingDirectory(this, "选择图片文件夹",
                                                     folderPath.isEmpty() ? QDir::homePath() : folderPath);
    if (folderPath.isEmpty()) return;

    QDir dir(folderPath);
    QStringList filters = {"*.jpg","*.png","*.jpeg","*.bmp","*.tiff","*.webp"};
    imageList = dir.entryList(filters, QDir::Files | QDir::Readable);
    imageList.sort();

    if (imageList.isEmpty()) {
        QMessageBox::warning(this, "提示", "所选文件夹中没有找到支持的图片格式！\n支持: jpg/png/jpeg/bmp/tiff/webp");
        return;
    }

    currentIndex = 0;
    updateImageListDisplay();
    loadImage();
    statusBar->showMessage(QString("已加载 %1 张图片").arg(imageList.size()), 5000);
}

void MainWindow::openSingleImage()
{
    QString filePath = QFileDialog::getOpenFileName(this, "打开单张图片", folderPath,
        "图片文件 (*.jpg *.png *.jpeg *.bmp *.tiff *.webp);;所有文件 (*)");
    if (filePath.isEmpty()) return;

    folderPath = QFileInfo(filePath).absolutePath();
    imageList.clear();

    QDir dir(folderPath);
    QStringList filters = {"*.jpg","*.png","*.jpeg","*.bmp","*.tiff","*.webp"};
    imageList = dir.entryList(filters, QDir::Files | QDir::Readable);
    imageList.sort();

    currentIndex = imageList.indexOf(QFileInfo(filePath).fileName());
    if (currentIndex < 0) currentIndex = 0;

    updateImageListDisplay();
    loadImage();
}

// ================= 图片显示与标注逻辑 =================
void MainWindow::loadImage()
{
    if (imageList.isEmpty() || currentIndex < 0 || currentIndex >= imageList.size()) return;

    view->clearBoxes();                 // ⚠️ 先清理 boxes（会 removeItem + delete），否则 scene->clear() 会先删掉对象导致悬垂指针
    scene->clear();                     // 再清空 scene 中剩余项（pixmap 等）
    // ⚠️ 注意：不在 loadImage() 中重置 currentRotation，这样 rotateLeft/rotateRight 调用 loadImage() 时旋转角度不会丢失
    // currentRotation 只在切换图片（nextImage/prevImage/onImageSelected）和手动 resetView() 时才重置

    QString path = folderPath + "/" + imageList[currentIndex];
    originalPixmap.load(path);

    if (originalPixmap.isNull()) {
        appendErrorLog(QString("无法加载图片: %1").arg(imageList[currentIndex]));
        return;
    }

    QPixmap displayPix = originalPixmap;
    if (currentRotation != 0) {
        QTransform rot;
        rot.rotate(currentRotation);
        displayPix = displayPix.transformed(rot);
    }

    QSize viewSize = view->size();
    QPixmap scaled = displayPix.scaled(viewSize - QSize(8, 8), Qt::KeepAspectRatio, Qt::SmoothTransformation);

    scene->addPixmap(scaled);
    view->setOriginalSize(displayPix.size());
    view->zoomFit();  // 自动适应画面

    loadLabelsFromFile(imageList[currentIndex]);

    imageListWidget->blockSignals(true);
    imageListWidget->setCurrentRow(currentIndex);
    imageListWidget->blockSignals(false);

    refreshStatusBar();
    appendLog(QString("[%1/%2] %3 | 标注:%4框")
              .arg(currentIndex + 1).arg(imageList.size())
              .arg(imageList[currentIndex]).arg(view->boxes.size()));
}

bool MainWindow::loadLabelsFromFile(const QString &imgName)
{
    QString txtFile = folderPath + "/" + imgName + ".txt";
    QFile f(txtFile);

    if (!f.exists() || !f.open(QIODevice::ReadOnly)) {
        QStringList dbLabels = DatabaseManager::loadAnnotations(folderPath + "/" + imgName);
        if (!dbLabels.empty()) {
            for (const QString &line : dbLabels) {
                QStringList parts = line.split(" ", Qt::SkipEmptyParts);
                if (parts.size() >= 5) {
                    int classId = parts[0].toInt();
                    float x = parts[1].toFloat() * originalPixmap.width();
                    float y = parts[2].toFloat() * originalPixmap.height();
                    float w = parts[3].toFloat() * originalPixmap.width();
                    float h = parts[4].toFloat() * originalPixmap.height();

                    QGraphicsRectItem *rectItem = new QGraphicsRectItem(x, y, w, h);
                    QColor classColor = QColor((classId * 37) % 200 + 55,
                                               (classId * 73) % 200 + 55,
                                               (classId * 113) % 200 + 55);
                    rectItem->setPen(QPen(classColor, 2));
                    rectItem->setBrush(QBrush(QColor(classColor.red(), classColor.green(), classColor.blue(), 30)));
                    this->scene->addItem(rectItem);
                    view->boxes.append(rectItem);

                    BoxInfo info;
                    info.classId = classId;
                    info.normX = parts[1].toFloat();
                    info.normY = parts[2].toFloat();
                    info.normW = parts[3].toFloat();
                    info.normH = parts[4].toFloat();
                    view->boxInfos.append(info);
                }
            }
            return true;
        }
        return false;
    }

    QTextStream in(&f);
    while (!in.atEnd()) {
        QString line = in.readLine().trimmed();
        if (line.isEmpty()) continue;
        QStringList parts = line.split(" ", Qt::SkipEmptyParts);
        if (parts.size() < 5) continue;

        int classId = parts[0].toInt();
        float normX = parts[1].toFloat();
        float normY = parts[2].toFloat();
        float normW = parts[3].toFloat();
        float normH = parts[4].toFloat();

        float px = normX * originalPixmap.width();
        float py = normY * originalPixmap.height();
        float pw = normW * originalPixmap.width();
        float ph = normH * originalPixmap.height();

        QGraphicsRectItem *rectItem = new QGraphicsRectItem(px, py, pw, ph);
        QColor classColor = QColor((classId * 37) % 200 + 55,
                                   (classId * 73) % 200 + 55,
                                   (classId * 113) % 200 + 55);
        rectItem->setPen(QPen(classColor, 2));
        rectItem->setBrush(QBrush(QColor(classColor.red(), classColor.green(), classColor.blue(), 30)));
        this->scene->addItem(rectItem);
        view->boxes.append(rectItem);

        BoxInfo info;
        info.classId = classId;
        info.normX = normX;
        info.normY = normY;
        info.normW = normW;
        info.normH = normH;
        view->boxInfos.append(info);
    }
    f.close();

    return true;
}

void MainWindow::saveLabels()
{
    if (imageList.isEmpty()) return;
    if (currentIndex < 0 || currentIndex >= imageList.size()) return;

    QString imgName = imageList[currentIndex];
    QString txtFile = folderPath + "/" + imgName + ".txt";

    if (view->boxes.isEmpty()) {
        if (QFile::exists(txtFile)) QFile::remove(txtFile);
        return;
    }

    QFile f(txtFile);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        appendErrorLog(QString("无法保存标注: %1").arg(txtFile));
        return;
    }

    QTextStream out(&f);
    QStringList labelsForDb;

    for (int i = 0; i < view->boxes.size(); ++i) {
        const BoxInfo &info = view->boxInfos[i];

        double nx = qBound(0.0, info.normX, 1.0);
        double ny = qBound(0.0, info.normY, 1.0);
        double nw = qBound(0.001, info.normW, 1.0);
        double nh = qBound(0.001, info.normH, 1.0);

        QString line = QString("%1 %2 %3 %4 %5")
                       .arg(info.classId)
                       .arg(nx, 0, 'f', 6)
                       .arg(ny, 0, 'f', 6)
                       .arg(nw, 0, 'f', 6)
                       .arg(nh, 0, 'f', 6);
        out << line << "\n";
        labelsForDb.append(line);
    }
    f.close();

    DatabaseManager::saveAnnotations(folderPath + "/" + imgName, labelsForDb);

    // 刷新图片列表中的勾选状态
    updateImageListDisplay();
}

void MainWindow::nextImage()
{
    if (imageList.isEmpty()) return;
    saveLabels();
    currentIndex++;
    if (currentIndex >= imageList.size())
        currentIndex = 0;
    currentRotation = 0;  // 切换图片时重置旋转
    loadImage();
}

void MainWindow::prevImage()
{
    if (imageList.isEmpty()) return;
    saveLabels();
    currentIndex--;
    if (currentIndex < 0)
        currentIndex = imageList.size() - 1;
    currentRotation = 0;  // 切换图片时重置旋转
    loadImage();
}

void MainWindow::onImageSelected(int row)
{
    if (row == currentIndex || row < 0 || row >= imageList.size()) return;
    saveLabels();
    currentIndex = row;
    currentRotation = 0;  // 切换图片时重置旋转
    loadImage();
}

void MainWindow::onCurrentImageChanged(int idx)
{
    Q_UNUSED(idx);
    refreshStatusBar();
}

// ================= 图片列表管理 =================
void MainWindow::updateImageListDisplay()
{
    imageListWidget->clear();
    int labeledCount = 0;
    for (int i = 0; i < imageList.size(); ++i) {
        QString name = imageList[i];
        QString txtFile = folderPath + "/" + name + ".txt";
        bool hasLabel = QFile::exists(txtFile);
        if (hasLabel) labeledCount++;

        QListWidgetItem *item = new QListWidgetItem(
            QString("%1. %2 %3")
            .arg(i + 1, 3)
            .arg(name)
            .arg(hasLabel ? "✅" : "⬜")
        );
        item->setData(Qt::UserRole, i);
        // 已标注的用绿色标记
        if (hasLabel) {
            item->setForeground(QColor("#2E7D32"));
        }
        imageListWidget->addItem(item);
    }
    
    // 更新统计信息
    if (listStatsLabel) {
        listStatsLabel->setText(QString("共%1张 | 已标注%2张 (%3%)")
            .arg(imageList.size())
            .arg(labeledCount)
            .arg(imageList.size() > 0 ? labeledCount * 100 / imageList.size() : 0));
    }
}

// ================= 标注操作 =================
void MainWindow::deleteBox()
{
    deleteSelectedBox();
}

void MainWindow::deleteSelectedBox()
{
    if (view->boxes.isEmpty()) return;

    int idxToDelete = view->selectedBoxIndex();
    if (idxToDelete < 0) idxToDelete = view->boxes.size() - 1;

    view->removeBoxWithUndo(idxToDelete);
    appendLog(QString("[删除] 第%1个标注框").arg(idxToDelete + 1));
}

void MainWindow::changeBoxClass()
{
    int idx = view->selectedBoxIndex();
    if (idx < 0) {
        QMessageBox::information(this, "提示", "请先点击一个标注框来选中它！");
        return;
    }
    if (labelBox->currentIndex() < 0) return;

    int newClass = labelBox->currentIndex();
    int oldClass = view->boxInfos[idx].classId;
    if (oldClass == newClass) return;

    QColor c = QColor((newClass * 37) % 200 + 55,
                      (newClass * 73) % 200 + 55,
                      (newClass * 113) % 200 + 55);
    view->changeClassWithUndo(idx, oldClass, newClass);
    // 更新画笔颜色
    view->boxes[idx]->setPen(QPen(c, 2));
    appendLog(QString("[改类] 第%1个框 → %2(%3)")
              .arg(idx + 1).arg(newClass).arg(classNames.value(newClass)));
}

void MainWindow::updateLabels()
{
    labelBox->clear();
    classNames.clear();
    QStringList items = labelInput->text().split(",", Qt::SkipEmptyParts);
    for (const auto &item : items) {
        QString trimmed = item.trimmed();
        if (trimmed.contains(":")) {
            QStringList kv = trimmed.split(":");
            if (kv.size() >= 2) {
                classNames.append(kv[1].trimmed());
                labelBox->addItem(kv[1].trimmed());
            }
        } else {
            classNames.append(trimmed);
            labelBox->addItem(trimmed);
        }
    }
    appendLog(QString("[标签] 更新为%1类: ").arg(classNames.size()) + classNames.join(", "));
}

// ================= 视图操作 =================
void MainWindow::zoomIn() { view->zoomIn(); currentZoomFactor *= 1.25; }
void MainWindow::zoomOut() { view->zoomOut(); currentZoomFactor *= 0.8; }
void MainWindow::zoomFit() { view->zoomFit(); currentZoomFactor = 1.0; }
void MainWindow::resetView() { view->resetView(); currentRotation = 0; currentZoomFactor = 1.0; }

void MainWindow::rotateLeft()
{
    currentRotation -= 90;
    if (currentIndex >= 0 && currentIndex < imageList.size())
        loadImage();
}

void MainWindow::rotateRight()
{
    currentRotation += 90;
    if (currentIndex >= 0 && currentIndex < imageList.size())
        loadImage();
}

// ================= 状态栏更新 =================
void MainWindow::refreshStatusBar()
{
    QString msg;
    if (!imageList.isEmpty() && currentIndex >= 0 && currentIndex < imageList.size()) {
        msg = QString("[%1/%2] %3 | 标注框: %4 | 缩放: %5%")
              .arg(currentIndex + 1).arg(imageList.size()).arg(imageList[currentIndex])
              .arg(view->boxes.size()).arg(qRound(currentZoomFactor * 100));
    } else {
        msg = "就绪";
    }
    statusBar->showMessage(msg);
}

// ================= 训练流程 =================
void MainWindow::selectDatasetDir()
{
    QString dir = QFileDialog::getExistingDirectory(this, "选择训练数据集",
                                                      datasetEdit->text().isEmpty() ? folderPath : datasetEdit->text());
    if (!dir.isEmpty()) datasetEdit->setText(dir);
}

void MainWindow::startTraining()
{
    // 先检查Python环境
    if (!pythonReady) {
        QMessageBox::critical(this, "Python环境错误",
            "❌ Python环境中未找到 ultralytics！\n\n"
            "请在终端执行:\n"
            "  conda activate zyx\n"
            "  pip install ultralytics onnxruntime opencv-python\n\n"
            "当前Python: " + pythonExePath +
            "\n缺失: " + pythonMissingModules.join(", "));
        appendErrorLog("训练启动失败: 缺少ultralytics");
        return;
    }

    QString dsPath = datasetEdit->text().trimmed();
    if (dsPath.isEmpty()) {
        QMessageBox::warning(this, "错误", "请先设置数据集路径！");
        return;
    }
    if (!QDir(dsPath).exists()) {
        QMessageBox::warning(this, "错误", "数据集路径不存在：" + dsPath);
        return;
    }

    QString script = getScriptPath("train.py");
    if (!QFile::exists(script)) {
        QMessageBox::warning(this, "错误", "找不到训练脚本:\n" + script);
        return;
    }

    int epochs = epochSpin->value();
    int batchSize = batchSpin->value();
    double splitRatio = splitSlider->value() / 100.0;

    QStringList args;
    args << "-u" << script
         << "--dataset" << dsPath
         << "--epochs" << QString::number(epochs)
         << "--batch" << QString::number(batchSize)
         << "--split" << QString::number(splitRatio, 'f', 2)
         << "--classes" << classNames.join(",")
         << "--output" << (dsPath + "/yolo_output");  // 固定输出目录

    trainStatusLabel->setText("状态: ⏳ 训练中...");
    trainStatusLabel->setStyleSheet("font-weight: bold; font-size: 12px; color: #1976D2;");
    trainProgressBar->setValue(0);
    activeTask = Training;
    accumulatedOutput.clear();

    appendLog("========== 开始训练 ==========", QColor("#1976D2"));
    appendLog(QString("数据集: %1 | Epochs: %2 | Batch: %3")
              .arg(dsPath).arg(epochs).arg(batchSize));
    appendLog(QString("类别: %1").arg(classNames.join(", ")));
    appendLog(QString("模型将保存至: %1/yolo_output/weights/").arg(dsPath));

    process->start(pythonExePath, args);

    if (!process->waitForStarted(3000)) {
        QMessageBox::critical(this, "错误", "无法启动Python进程！\n" + pythonExePath);
        trainStatusLabel->setText("状态: ❌ 启动失败");
        trainStatusLabel->setStyleSheet("font-weight: bold; font-size: 12px; color: #D32F2F;");
        activeTask = None;
    }
}

void MainWindow::onTrainingFinished(int exitCode, QProcess::ExitStatus status)
{
    QString remainingStderr = QString::fromUtf8(process->readAllStandardError()).trimmed();

    bool success = (status == QProcess::NormalExit && exitCode == 0);

    if (!remainingStderr.isEmpty()) {
        appendLog("[stderr] " + remainingStderr, QColor("#FF9800"));
    }

    if (success) {
        trainStatusLabel->setText("状态: ✅ 完成");
        trainStatusLabel->setStyleSheet("font-weight: bold; font-size: 12px; color: #2E7D32;");
        trainProgressBar->setValue(100);
        appendSuccessLog("========== 训练完成 ==========");

        // 查找模型
        QString dsPath = datasetEdit->text().trimmed();
        QString modelBaseDir = dsPath + "/yolo_output/";

        QStringList searchPaths = {
            modelBaseDir + "runs/train/weights/best.pt",
            modelBaseDir + "runs/train/weights/last.pt",
            dsPath + "/runs/detect/train/weights/best.pt",
            dsPath + "/runs/detect/train/weights/last.pt",
        };

        QString bestModelPath;
        for (const auto &p : searchPaths) {
            if (QFile::exists(p)) {
                bestModelPath = p;
                break;
            }
        }

        // 解析训练输出中的 mAP 指标
        float bestMap50 = 0.0f;
        float bestMap5095 = 0.0f;
        QString fullOutput = accumulatedOutput;

        // 优先解析结构化 [METRICS] 行
        QRegularExpression metricsRe(R"(\[METRICS\]\s+mAP50=([\d.]+)\s+mAP50-95=([\d.]+))");
        QRegularExpressionMatch metricsMatch = metricsRe.match(fullOutput);
        if (metricsMatch.hasMatch()) {
            bestMap50 = metricsMatch.captured(1).toFloat();
            bestMap5095 = metricsMatch.captured(2).toFloat();
        }

        // 解析 mAP50 (回退)
        if (bestMap50 == 0) {
            QRegularExpression map50Re(R"(mAP50\S*\s*:\s*([\d.]+))");
            QRegularExpressionMatch m50 = map50Re.match(fullOutput);
            if (m50.hasMatch()) bestMap50 = m50.captured(1).toFloat();
        }

        // 解析 mAP50-95 (回退)
        if (bestMap5095 == 0) {
            QRegularExpression map5095Re(R"(mAP50-95\S*\s*:\s*([\d.]+))");
            QRegularExpressionMatch m5095 = map5095Re.match(fullOutput);
            if (m5095.hasMatch()) bestMap5095 = m5095.captured(1).toFloat();
        }

        // 也尝试解析 [DONE] 行中的指标
        QRegularExpression doneRe(R"(mAP50:\s*([\d.]+).*mAP50-95:\s*([\d.]+))");
        QRegularExpressionMatch doneMatch = doneRe.match(fullOutput);
        if (doneMatch.hasMatch()) {
            if (bestMap50 == 0) bestMap50 = doneMatch.captured(1).toFloat();
            if (bestMap5095 == 0) bestMap5095 = doneMatch.captured(2).toFloat();
        }

        // 显示指标
        if (bestMap50 > 0) {
            appendSuccessLog(QString("📊 mAP50: %1%  |  mAP50-95: %2%")
                            .arg(bestMap50 * 100, 0, 'f', 1)
                            .arg(bestMap5095 * 100, 0, 'f', 1));
        }

        if (!bestModelPath.isEmpty()) {
            appendSuccessLog("最佳模型: " + bestModelPath);
            QFileInfo fi(bestModelPath);
            if (fi.exists()) {
                double sizeMB = fi.size() / (1024.0 * 1024.0);
                appendSuccessLog(QString("模型大小: %1 MB").arg(sizeMB, 0, 'f', 1));
            }

            // 保存到数据库
            QDateTime now = QDateTime::currentDateTime();
            float mapToSave = (bestMap50 > 0) ? bestMap50 * 100.0f : 0.0f;
            DatabaseManager::saveTrainingRecord(
                dsPath, epochSpin->value(), batchSpin->value(),
                bestModelPath, mapToSave,
                now.toString("yyyy-MM-dd hh:mm:ss")
            );

            // 自动转换 ONNX
            if (autoOnnxCheck->isChecked()) {
                autoConvertToOnnx(bestModelPath);
            }

            // 展示训练结果对话框
            showTrainingResultDialog(bestMap50, bestMap5095, bestModelPath);
        } else {
            // 广泛搜索
            QDir searchRoot(dsPath);
            QStringList found = searchRoot.entryList({"*.pt"}, QDir::Files);
            if (!found.isEmpty()) {
                appendSuccessLog("找到模型: " + dsPath + "/" + found.last());
            } else {
                appendErrorLog("未找到训练的模型文件！");
            }
            appendLog("提示: 模型保存在 yolo_output/runs/ 或 runs/ 下", QColor("#FF9800"));
        }
    } else {
        trainStatusLabel->setText("状态: ❌ 失败(码:" + QString::number(exitCode) + ")");
        trainStatusLabel->setStyleSheet("font-weight: bold; font-size: 12px; color: #D32F2F;");
        trainProgressBar->setValue(0);

        appendErrorLog("========== 训练失败 ==========");
        appendErrorLog(QString("退出码: %1").arg(exitCode));

        QString errorMsg = "训练失败！\n\n";
        if (remainingStderr.contains("ModuleNotFoundError")) {
            QRegularExpression modRe("No module named '([^']+)'");
            QRegularExpressionMatch match = modRe.match(remainingStderr);
            QString missingModule = match.hasMatch() ? match.captured(1) : "未知";
            errorMsg += "原因: 缺少 '" + missingModule + "'\n\n解决: pip install " + missingModule;
            appendErrorLog("缺少模块: " + missingModule);
        } else if (exitCode != 0) {
            errorMsg += "退出码: " + QString::number(exitCode) + "\n\n请查看日志区的详细错误信息。";
        }
        errorMsg += "\n\nPython: " + pythonExePath;

        QMessageBox::critical(this, "训练失败 ❌", errorMsg);
    }
}

void MainWindow::autoConvertToOnnx(const QString &ptPath)
{
    if (!pythonReady || ptPath.isEmpty()) return;

    QString script = getScriptPath("convert_onnx.py");
    if (!QFile::exists(script)) {
        appendLog("⚠ 找不到 convert_onnx.py，跳过自动转换", QColor("#FF9800"));
        return;
    }

    appendLog("\n========== 自动转换 ONNX ==========", QColor("#E65100"));
    appendLog(QString("输入: %1").arg(ptPath));

    QProcess onnxProc;
    QStringList args;
    args << "-u" << script << "--input" << ptPath;
    onnxProc.start(pythonExePath, args);

    if (onnxProc.waitForFinished(60000)) {
        QString output = QString::fromUtf8(onnxProc.readAllStandardOutput()).trimmed();
        QString error = QString::fromUtf8(onnxProc.readAllStandardError()).trimmed();

        if (!output.isEmpty()) appendLog(output);
        if (!error.isEmpty()) appendLog("[stderr] " + error, QColor("#FF9800"));

        if (onnxProc.exitCode() == 0) {
            appendSuccessLog("✅ ONNX 转换成功！");
            // 尝试更新推理模型路径
            QFileInfo fi(ptPath);
            QString onnxPath = fi.path() + "/" + fi.completeBaseName() + ".onnx";
            if (QFile::exists(onnxPath)) {
                inferenceModelEdit->setText(onnxPath);
                appendSuccessLog("推理模型已自动设置: " + onnxPath);
                QMessageBox::information(this, "全部完成 ✅",
                    QString("训练 + ONNX 转换已完成！\n\n"
                    "模型路径:\n  %1\n\n"
                    "推理模型已自动填入，可直接切换到「推理」Tab 进行测试。")
                    .arg(onnxPath));
            }
        } else {
            appendErrorLog("ONNX 转换失败，退出码: " + QString::number(onnxProc.exitCode()));
        }
    } else {
        appendErrorLog("ONNX 转换超时 (>60s)");
    }
}

void MainWindow::readTrainingOutput()
{
    readOutput();
}

void MainWindow::readOutput()
{
    QByteArray out = process->readAllStandardOutput();
    QByteArray err = process->readAllStandardError();

    if (!out.isEmpty()) {
        QString outStr = QString::fromUtf8(out).trimmed();
        accumulatedOutput += outStr + "\n";  // 累积完整输出
        if (outStr.contains("Error", Qt::CaseInsensitive) ||
            outStr.contains("Traceback", Qt::CaseInsensitive)) {
            appendLog(outStr, QColor("#FF9800"));
        } else if (outStr.contains("[DONE]") || outStr.contains("Results saved")) {
            appendLog(outStr, QColor("#81C784"));
        } else {
            appendLog(outStr);
        }

        // 解析训练进度
        if (activeTask == Training) {
            static QRegularExpression re(R"(Epoch\s+(\d+)/(\d+))");
            QRegularExpressionMatch match = re.match(outStr);
            if (match.hasMatch() && epochSpin->value() > 0) {
                int cur = match.captured(1).toInt();
                int total = match.captured(2).toInt();
                trainProgressBar->setValue(cur * 100 / total);
                trainStatusLabel->setText(QString("状态: ⏳ Epoch %1/%2").arg(cur).arg(total));
            }
        }
    }

    if (!err.isEmpty()) {
        QString errStr = QString::fromUtf8(err).trimmed();
        appendLog("[stderr] " + errStr, QColor("#FF9800"));

        if (errStr.contains("ModuleNotFoundError")) {
            appendErrorLog("⚠️ 检测到模块缺失错误！");
        }
    }

    // 量化进度解析
    QString combined = QString::fromUtf8(out) + QString::fromUtf8(err);
    if (combined.contains("PROGRESS:")) {
        int start = combined.indexOf("PROGRESS:") + 9;
        int end = combined.indexOf("%", start);
        if (end > start) {
            int val = combined.mid(start, end - start).toInt();
            quantProgressBar->setValue(val);
            quantStatusLabel->setText(QString("量化进度: %1%").arg(val));
        }
    }
}

void MainWindow::finished()
{
    appendLog("[系统] 后台进程已结束。");
}

// ================= 模型量化 =================
void MainWindow::selectOnnxModelForQuantize()
{
    QString path = QFileDialog::getOpenFileName(this, "选择模型进行量化",
        onnxInputEdit->text().isEmpty() ? getModelPath("") : onnxInputEdit->text(),
        "模型文件 (*.pt *.onnx);;PyTorch模型 (*.pt);;ONNX模型 (*.onnx);;所有文件 (*)");
    if (!path.isEmpty()) onnxInputEdit->setText(path);
}

void MainWindow::startQuantization()
{
    if (!pythonReady) {
        QMessageBox::critical(this, "Python环境错误", "❌ Python 环境未就绪！\npip install ultralytics onnx onnxruntime");
        appendErrorLog("量化失败: Python环境未就绪");
        return;
    }

    QString inputModel = onnxInputEdit->text().trimmed();
    if (inputModel.isEmpty()) {
        // 优先搜索 .pt，其次 .onnx
        inputModel = getModelPath("best.pt");
        if (!QFile::exists(inputModel))
            inputModel = getModelPath("best.onnx");
    }
    if (!QFile::exists(inputModel)) {
        QMessageBox::warning(this, "错误", "找不到模型:\n" + inputModel);
        return;
    }

    QString script = getScriptPath("quantize.py");
    if (!QFile::exists(script)) {
        QMessageBox::warning(this, "错误", "找不到量化脚本:\n" + script);
        return;
    }

    // 量化类型：从下拉框获取
    QString qTypeStr = quantTypeCombo->currentText().contains("INT8") ? "INT8" : "FP16";

    QStringList args;
    args << "-u" << script
         << "--input" << inputModel
         << "--type" << qTypeStr;

    quantStatusLabel->setText("状态: ⏳ 量化中...");
    quantStatusLabel->setStyleSheet("font-weight: bold; font-size: 12px; color: #1976D2;");
    quantProgressBar->setValue(0);
    activeTask = Quantization;

    appendLog("========== 开始量化 ==========", QColor("#7B1FA2"));
    appendLog(QString("输入: %1 | 类型: %2").arg(inputModel).arg(qTypeStr));

    process->start(pythonExePath, args);

    if (!process->waitForStarted(3000)) {
        quantStatusLabel->setText("状态: ❌ 启动失败");
        quantStatusLabel->setStyleSheet("font-weight: bold; font-size: 12px; color: #D32F2F;");
        activeTask = None;
    }
}

void MainWindow::onQuantizationFinished(int exitCode, QProcess::ExitStatus status)
{
    QString remainingStderr = QString::fromUtf8(process->readAllStandardError()).trimmed();
    bool success = (status == QProcess::NormalExit && exitCode == 0);

    if (!remainingStderr.isEmpty()) {
        appendLog("[stderr] " + remainingStderr, QColor("#FF9800"));
    }

    if (success) {
        quantStatusLabel->setText("状态: ✅ 完成");
        quantStatusLabel->setStyleSheet("font-weight: bold; font-size: 12px; color: #2E7D32;");
        quantProgressBar->setValue(100);
        appendSuccessLog("========== 量化完成 ==========");

        // 量化完成后自动查找输出文件（best_quantized.onnx 和 best.onnx）
        QString inputModel = onnxInputEdit->text().trimmed();
        QFileInfo fi(inputModel);
        QString modelDir = fi.absolutePath();

        // 查找量化模型：优先 best_quantized.onnx（同目录）
        QStringList quantCandidates = {
            modelDir + "/best_quantized.onnx",
            fi.path() + "/" + fi.completeBaseName() + "_quantized.onnx",
        };
        QString foundQuant;
        for (const auto &c : quantCandidates) {
            if (QFile::exists(c)) { foundQuant = c; break; }
        }

        // 查找 FP32 ONNX（best.onnx）
        QString fp32Onnx = modelDir + "/best.onnx";

        QString msg = "模型量化成功！\n\n";
        if (!foundQuant.isEmpty()) {
            appendSuccessLog("量化模型 (FP16): " + foundQuant);
            inferenceModelEdit->setText(foundQuant);
            msg += "📦 量化模型 (FP16 .onnx):\n" + foundQuant + "\n\n";
        }
        if (QFile::exists(fp32Onnx)) {
            appendSuccessLog("FP32 模型 (best.onnx): " + fp32Onnx);
            msg += "📦 原始精度 (FP32 .onnx):\n" + fp32Onnx + "\n\n";
        }
        msg += "📌 提示：量化前后精度对比\n"
               "  • .pt 文件 → FP32 精度（最高）\n"
               "  • best.onnx → FP32 精度（同 .pt）\n"
               "  • best_quantized.onnx → FP16 精度（降低，体积减半）";

        QMessageBox::information(this, "量化完成 ✅", msg);
    } else {
        quantStatusLabel->setText("状态: ❌ 失败(码:" + QString::number(exitCode) + ")");
        quantStatusLabel->setStyleSheet("font-weight: bold; font-size: 12px; color: #D32F2F;");
        quantProgressBar->setValue(0);
        appendErrorLog("量化失败");
        QMessageBox::critical(this, "量化失败 ❌", "退出码: " + QString::number(exitCode));
    }
}

// ================= 推理 =================
void MainWindow::selectInferenceModel()
{
    QString path = QFileDialog::getOpenFileName(this, "选择推理模型",
        inferenceModelEdit->text().isEmpty() ? getModelPath("") : inferenceModelEdit->text(),
        "模型文件 (*.pt *.onnx);;PyTorch模型 (*.pt);;ONNX模型 (*.onnx);;所有文件 (*)");
    if (!path.isEmpty()) inferenceModelEdit->setText(path);
}

void MainWindow::selectInferenceImages()
{
    QString path = QFileDialog::getOpenFileName(this, "选择推理图片",
        QDir::homePath(), "图片文件 (*.jpg *.jpeg *.png *.bmp);;所有文件 (*)");
    if (!path.isEmpty())
        inferenceImageEdit->setText(path);
}

void MainWindow::runInferencePython()
{
    if (!pythonReady) {
        QMessageBox::critical(this, "Python环境错误",
            "❌ Python 环境未就绪！\n"
            "请安装: pip install ultralytics onnxruntime opencv-python\n\n"
            "提示: .pt 模型需 ultralytics，.onnx 模型需 onnxruntime");
        return;
    }

    // 优先使用推理 Tab 中选中的图片，否则用标注视图当前图片
    QString imgPath = inferenceImageEdit->text().trimmed();
    if (imgPath.isEmpty()) {
        if (imageList.isEmpty()) {
            QMessageBox::warning(this, "错误", "请先在推理 Tab 中选择一张图片！\n或在标注 Tab 中加载图片。");
            return;
        }
        imgPath = folderPath + "/" + imageList[currentIndex];
    }

    QString modelPath = inferenceModelEdit->text().trimmed();
    if (modelPath.isEmpty()) {
        // 优先 .pt，其次 .onnx
        QString ptPath = getModelPath("best.pt");
        if (QFile::exists(ptPath)) {
            modelPath = ptPath;
        } else {
            modelPath = getModelPath("best.onnx");
        }
    }

    if (!QFile::exists(modelPath)) {
        QMessageBox::warning(this, "错误", "模型不存在:\n" + modelPath);
        return;
    }

    QString script = getScriptPath("inference.py");
    QStringList args;
    args << "-u" << script << "--image" << imgPath << "--model" << modelPath;

    appendLog("\n========== Python推理 ==========", QColor("#4CAF50"));

    QProcess p;
    p.start(pythonExePath, args);
    if (!p.waitForFinished(30000)) {
        inferenceResultLabel->setText("结果: ⏳ 超时");
        appendErrorLog("推理超时 (>30s)");
        return;
    }

    QString output = QString::fromUtf8(p.readAllStandardOutput()).trimmed();
    QString error = QString::fromUtf8(p.readAllStandardError()).trimmed();
    if (!error.isEmpty()) appendLog("stderr: " + error, QColor("#FF9800"));

    if (p.exitCode() != 0) {
        inferenceResultLabel->setText("结果: ❌ 失败");
        inferenceResultLabel->setStyleSheet("font-weight:bold; color:#D32F2F; font-size:16px; padding:8px; border-radius:6px; background:#FFEBEE;");
        appendErrorLog("Python推理失败, 退出码: " + QString::number(p.exitCode()));
        return;
    }

    if (!output.isEmpty()) {
        // 解析推理输出
        QString resultSummary;
        QString bestClass;
        float bestConf = 0.0f;

        // 解析 [RESULT] 行，格式:
        //   ✅ 中文名(英文名) | 置信度: 0.xxx | 框: [...]
        QRegularExpression resultRe(R"(✅\s+(\S+)\((\S+)\)\s*\|\s*置信度:\s*([\d.]+))");
        QRegularExpressionMatchIterator it = resultRe.globalMatch(output);

        QStringList resultLines;
        while (it.hasNext()) {
            QRegularExpressionMatch m = it.next();
            QString zhName = m.captured(1);
            QString enName = m.captured(2);
            float conf = m.captured(3).toFloat();
            resultLines.append(QString("%1(%2): %3%")
                              .arg(zhName).arg(enName)
                              .arg(conf * 100, 0, 'f', 1));
            if (conf > bestConf) {
                bestConf = conf;
                bestClass = zhName;
            }
        }

        if (resultLines.isEmpty()) {
            // 回退：尝试匹配单行
            QRegularExpression simpleRe(R"(✅\s+(.+?)\s*\|\s*置信度:\s*([\d.]+))");
            QRegularExpressionMatch sm = simpleRe.match(output);
            if (sm.hasMatch()) {
                bestClass = sm.captured(1);
                bestConf = sm.captured(2).toFloat();
                resultSummary = QString("%1  (置信度: %2%)")
                               .arg(bestClass).arg(bestConf * 100, 0, 'f', 1);
            } else {
                resultSummary = "推理完成，详见日志";
            }
        } else {
            resultSummary = resultLines.join("\n");
        }

        // 更新 UI
        inferenceResultLabel->setText("结果: " + (resultLines.isEmpty() ? resultSummary : resultLines.first()));
        inferenceResultLabel->setStyleSheet("font-weight:bold; color:#2E7D32; font-size:16px; padding:8px; border-radius:6px; background:#E8F5E9;");
        appendSuccessLog("预测: " + resultSummary);

        // 保存到数据库
        DatabaseManager::saveInferenceRecord(
            imgPath,
            bestClass.isEmpty() ? resultSummary : bestClass,
            bestConf,
            modelPath.endsWith(".pt") ? "PyTorch (.pt)" :
            (modelPath.contains("quantized") ? "ONNX Quantized" : "ONNX"),
            "Python"
        );

        // 展示结果弹窗
        showInferenceResultDialog(imgPath, resultSummary);
    }
}

// ================= C++ 端 ONNX 推理 =================
// YOLOv8 ONNX 输出是 (1, 4+N, 8400) 检测格式，OpenCV DNN 无法正确解析。
// 实际调用 Python onnxruntime 引擎完成推理，确保稳定可用。
bool MainWindow::doCppInference(const QString &imgPath, const QString &modelPath)
{
    QString script = getScriptPath("inference.py");
    if (!QFile::exists(script)) {
        appendErrorLog("C++推理: 找不到 inference.py, 无法完成");
        inferenceResultLabel->setText("结果[C++]: ❌ 缺少推理脚本");
        inferenceResultLabel->setStyleSheet(
            "font-weight:bold; color:#D32F2F; font-size:14px; padding:8px; "
            "border-radius:6px; background:#FFEBEE;");
        return false;
    }

    appendLog("(C++模式内部使用 Python ONNX Runtime 引擎)", QColor("#9E9E9E"));

    QProcess p;
    QStringList args;
    args << "-u" << script << "--image" << imgPath << "--model" << modelPath;
    p.start(pythonExePath, args);

    if (!p.waitForFinished(60000)) {
        inferenceResultLabel->setText("结果[C++]: ⏳ 超时");
        appendErrorLog("C++推理超时 (>60s)");
        return false;
    }

    QString output = QString::fromUtf8(p.readAllStandardOutput()).trimmed();
    QString error = QString::fromUtf8(p.readAllStandardError()).trimmed();
    if (!error.isEmpty()) appendLog("[stderr] " + error, QColor("#FF9800"));

    if (p.exitCode() != 0) {
        inferenceResultLabel->setText("结果[C++]: ❌ 失败");
        inferenceResultLabel->setStyleSheet(
            "font-weight:bold; color:#D32F2F; font-size:14px; padding:8px; "
            "border-radius:6px; background:#FFEBEE;");
        appendErrorLog(QString("C++推理失败, 退出码: %1").arg(p.exitCode()));
        return false;
    }

    // 解析 [RESULT] 行，格式: ✅ 中文名(英文名) | 置信度: 0.xxx | 框: [...]
    QString bestClass;
    float bestConf = 0.0f;
    QStringList resultLines;

    QRegularExpression resultRe(R"(✅\s+(\S+)\((\S+)\)\s*\|\s*置信度:\s*([\d.]+))");
    QRegularExpressionMatchIterator it = resultRe.globalMatch(output);
    while (it.hasNext()) {
        QRegularExpressionMatch m = it.next();
        QString zhName = m.captured(1);
        QString enName = m.captured(2);
        float conf = m.captured(3).toFloat();
        resultLines.append(QString("%1(%2): %3%")
                          .arg(zhName).arg(enName)
                          .arg(conf * 100, 0, 'f', 1));
        if (conf > bestConf) {
            bestConf = conf;
            bestClass = zhName;
        }
    }

    if (resultLines.isEmpty()) {
        // 回退匹配
        QRegularExpression simpleRe(R"(✅\s+(.+?)\s*\|\s*置信度:\s*([\d.]+))");
        QRegularExpressionMatch sm = simpleRe.match(output);
        if (sm.hasMatch()) {
            bestClass = sm.captured(1);
            bestConf = sm.captured(2).toFloat();
        }
    }

    QString resultSummary = resultLines.isEmpty()
        ? QString("推理完成")
        : resultLines.join("\n");

    inferenceResultLabel->setText(
        QString("结果[C++]: %1 (%2%)")
        .arg(bestClass.isEmpty() ? "未知" : bestClass)
        .arg(bestConf * 100, 0, 'f', 1));
    inferenceResultLabel->setStyleSheet(
        "font-weight:bold; color:#2E7D32; font-size:15px; padding:8px; "
        "border-radius:6px; background:#E8F5E9;");

    // 保存推理记录 + 标记引擎为 C++ 模式
    DatabaseManager::saveInferenceRecord(
        imgPath,
        bestClass.isEmpty() ? resultSummary : bestClass,
        bestConf,
        modelPath.endsWith(".pt") ? "PyTorch (.pt)" :
        (modelPath.contains("quantized") ? "ONNX Quantized" : "ONNX"),
        "C++ (ONNX Runtime)"
    );

    appendSuccessLog(QString("C++推理 → %1").arg(resultSummary));

    // 展示结果弹窗
    showInferenceResultDialog(imgPath, resultSummary);
    return true;
}

void MainWindow::runInferenceCpp()
{
    // 优先使用推理 Tab 中选中的图片，否则用标注视图当前图片
    QString imgPath = inferenceImageEdit->text().trimmed();
    if (imgPath.isEmpty()) {
        if (imageList.isEmpty()) {
            QMessageBox::warning(this, "错误", "请先在推理 Tab 中选择一张图片！\n或在标注 Tab 中加载图片。");
            return;
        }
        imgPath = folderPath + "/" + imageList[currentIndex];
    }

    QString modelPath = inferenceModelEdit->text().trimmed();
    if (modelPath.isEmpty()) {
        // 优先 .pt，其次 .onnx
        QString ptPath = getModelPath("best.pt");
        if (QFile::exists(ptPath)) {
            modelPath = ptPath;
        } else {
            modelPath = getModelPath("best.onnx");
        }
    }

    if (!QFile::exists(modelPath)) {
        QMessageBox::warning(this, "错误", "模型不存在:\n" + modelPath);
        return;
    }

    appendLog("\n========== C++ (ONNX Runtime) 推理 ==========", QColor("#4CAF50"));
    appendLog(QString("图片: %1").arg(imgPath));
    appendLog(QString("模型: %1").arg(modelPath));

    doCppInference(imgPath, modelPath);
}

void MainWindow::onPythonCheckFinished(int exitCode, QProcess::ExitStatus status)
{
    Q_UNUSED(exitCode);
    Q_UNUSED(status);
}

// ================= 数据库查看 =================
void MainWindow::showDatasetInfo()
{
    if (imageList.isEmpty()) {
        QMessageBox::information(this, "提示", "请先打开图片文件夹！");
        return;
    }

    QDialog dlg(this);
    dlg.setWindowTitle("数据集概览");
    dlg.resize(500, 400);

    QVBoxLayout *layout = new QVBoxLayout(&dlg);
    layout->setSpacing(10);

    QLabel *title = new QLabel("📊 当前数据集统计");
    title->setStyleSheet("font-size:16px; font-weight:bold; color:#1976D2; padding:6px;");
    layout->addWidget(title);

    // 统计数据
    int totalImages = imageList.size();
    int labeledCount = 0;
    int totalBoxes = 0;
    QMap<int, int> classCount;  // 类别 → 框数量

    for (int i = 0; i < imageList.size(); ++i) {
        QString txtFile = folderPath + "/" + imageList[i] + ".txt";
        if (QFile::exists(txtFile)) {
            labeledCount++;
            QFile f(txtFile);
            if (f.open(QIODevice::ReadOnly)) {
                QTextStream in(&f);
                while (!in.atEnd()) {
                    QString line = in.readLine().trimmed();
                    if (!line.isEmpty()) {
                        totalBoxes++;
                        QStringList parts = line.split(" ", Qt::SkipEmptyParts);
                        if (parts.size() >= 5) {
                            int cls = parts[0].toInt();
                            classCount[cls]++;
                        }
                    }
                }
                f.close();
            }
        }
    }

    QString info;
    info += QString("📁 总图片数:  %1 张\n\n").arg(totalImages);
    info += QString("✅ 已标注:    %1 张 (%2%)\n")
            .arg(labeledCount)
            .arg(totalImages > 0 ? labeledCount * 100 / totalImages : 0);
    info += QString("⬜ 未标注:    %1 张\n\n").arg(totalImages - labeledCount);
    info += QString("📦 总标注框:  %1 个\n").arg(totalBoxes);
    if (labeledCount > 0)
        info += QString("📐 平均每张:  %1 框\n\n").arg((double)totalBoxes / labeledCount, 0, 'f', 1);

    info += "🏷 类别分布:\n";
    info += "───────────────────\n";
    QMapIterator<int, int> it(classCount);
    QStringList colorCodes = {"🔴","🟢","🔵","🟡","🟣","🟠","⚪","🟤","⚫"};
    while (it.hasNext()) {
        it.next();
        QString clsName = classNames.value(it.key(), QString("类别%1").arg(it.key()));
        QString dot = colorCodes.value(it.key(), "⬤");
        double pct = totalBoxes > 0 ? (double)it.value() / totalBoxes * 100 : 0;
        QString bar = QString("█").repeated(qMax(1, (int)(pct / 5)));
        info += QString("  %1 %2: %3个 (%4%) %5\n")
                .arg(dot).arg(clsName).arg(it.value()).arg(pct, 0, 'f', 1).arg(bar);
    }

    QLabel *infoLabel = new QLabel(info);
    infoLabel->setFont(QFont("Menlo", 11));
    infoLabel->setStyleSheet("padding:12px; background:#F5F5F5; border-radius:8px; border:1px solid #DDD;");
    infoLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(infoLabel);

    QPushButton *btnClose = new QPushButton("关闭");
    btnClose->setMinimumHeight(30);
    btnClose->setStyleSheet(
        "QPushButton { background:#1976D2; color:white; border:none; border-radius:6px; font-size:12px; }"
        "QPushButton:hover { background:#1E88E5; }");
    layout->addWidget(btnClose);
    connect(btnClose, &QPushButton::clicked, &dlg, &QDialog::accept);

    dlg.exec();
}
void MainWindow::showTrainingHistory()
{
    QDialog dlg(this);
    dlg.setWindowTitle("训练历史记录");
    dlg.resize(850, 450);

    QVBoxLayout *layout = new QVBoxLayout(&dlg);
    QTableWidget *table = new QTableWidget();
    table->setColumnCount(7);
    table->setHorizontalHeaderLabels({"ID", "数据集", "Epochs", "Batch", "mAP50(%)", "模型路径", "时间"});
    table->horizontalHeader()->setStretchLastSection(true);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setAlternatingRowColors(true);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);

    QList<QVariantMap> records = DatabaseManager::getTrainingRecords();
    table->setRowCount(records.size());

    for (int i = 0; i < records.size(); ++i) {
        const QVariantMap &r = records[i];
        table->setItem(i, 0, new QTableWidgetItem(r["id"].toString()));
        table->setItem(i, 1, new QTableWidgetItem(r["dataset_path"].toString()));
        table->setItem(i, 2, new QTableWidgetItem(r["epochs"].toString()));
        table->setItem(i, 3, new QTableWidgetItem(r["batch_size"].toString()));
        float mapVal = r["best_map"].toFloat();
        QTableWidgetItem *mapItem = new QTableWidgetItem(
            mapVal > 0 ? QString::number(mapVal, 'f', 1) : "N/A");
        if (mapVal > 70) mapItem->setForeground(QColor("#2E7D32"));
        else if (mapVal > 40) mapItem->setForeground(QColor("#E65100"));
        else if (mapVal > 0) mapItem->setForeground(QColor("#D32F2F"));
        table->setItem(i, 4, mapItem);
        table->setItem(i, 5, new QTableWidgetItem(r["model_path"].toString()));
        table->setItem(i, 6, new QTableWidgetItem(r["train_time"].toString()));
    }

    table->resizeColumnsToContents();
    layout->addWidget(table);

    QHBoxLayout *btnRow = new QHBoxLayout;
    QPushButton *btnClose = new QPushButton("关闭");
    QPushButton *btnRefresh = new QPushButton("刷新");
    btnRow->addStretch();
    btnRow->addWidget(btnRefresh);
    btnRow->addWidget(btnClose);
    layout->addLayout(btnRow);

    connect(btnRefresh, &QPushButton::clicked, [table]() {
        QList<QVariantMap> newRecords = DatabaseManager::getTrainingRecords();
        table->setRowCount(newRecords.size());
        for (int i = 0; i < newRecords.size(); ++i) {
            const QVariantMap &r = newRecords[i];
            table->setItem(i, 0, new QTableWidgetItem(r["id"].toString()));
            table->setItem(i, 1, new QTableWidgetItem(r["dataset_path"].toString()));
            table->setItem(i, 2, new QTableWidgetItem(r["epochs"].toString()));
            table->setItem(i, 3, new QTableWidgetItem(r["batch_size"].toString()));
            table->setItem(i, 4, new QTableWidgetItem(QString::number(r["best_map"].toFloat(), 'f', 1)));
            table->setItem(i, 5, new QTableWidgetItem(r["model_path"].toString()));
            table->setItem(i, 6, new QTableWidgetItem(r["train_time"].toString()));
        }
        table->resizeColumnsToContents();
    });
    connect(btnClose, &QPushButton::clicked, &dlg, &QDialog::accept);
    dlg.exec();
}

void MainWindow::showInferenceHistory()
{
    QDialog dlg(this);
    dlg.setWindowTitle("推理历史记录");
    dlg.resize(900, 500);

    QVBoxLayout *layout = new QVBoxLayout(&dlg);
    QTableWidget *table = new QTableWidget();
    table->setColumnCount(7);
    table->setHorizontalHeaderLabels({"ID", "图片路径", "预测类别", "置信度", "模型类型", "推理模式", "时间"});
    table->horizontalHeader()->setStretchLastSection(true);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setAlternatingRowColors(true);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);

    QList<QVariantMap> records = DatabaseManager::getInferenceRecords();
    table->setRowCount(records.size());

    for (int i = 0; i < records.size(); ++i) {
        const QVariantMap &r = records[i];
        table->setItem(i, 0, new QTableWidgetItem(r["id"].toString()));
        table->setItem(i, 1, new QTableWidgetItem(r["image_path"].toString()));
        table->setItem(i, 2, new QTableWidgetItem(r["predicted_class"].toString()));

        float conf = r["confidence"].toFloat();
        QTableWidgetItem *confItem = new QTableWidgetItem(
            conf > 0 ? QString::number(conf * 100, 'f', 1) + "%" : r["confidence"].toString());
        if (conf > 0.6) confItem->setForeground(QColor("#2E7D32"));
        else if (conf > 0.3) confItem->setForeground(QColor("#E65100"));
        table->setItem(i, 3, confItem);

        table->setItem(i, 4, new QTableWidgetItem(r["model_type"].toString()));
        table->setItem(i, 5, new QTableWidgetItem(r["inference_mode"].toString()));
        table->setItem(i, 6, new QTableWidgetItem(r["created_at"].toString()));
    }

    table->resizeColumnsToContents();
    layout->addWidget(table);

    QHBoxLayout *btnRow = new QHBoxLayout;
    QPushButton *btnClose = new QPushButton("关闭");
    QPushButton *btnExport = new QPushButton("导出CSV");
    btnRow->addWidget(btnExport);
    btnRow->addStretch();
    btnRow->addWidget(btnClose);
    layout->addLayout(btnRow);

    connect(btnExport, &QPushButton::clicked, [&dlg]() {
        QString path = QFileDialog::getSaveFileName(&dlg, "导出推理记录", "inference_records.csv", "CSV (*.csv)");
        if (path.isEmpty()) return;
        QFile f(path);
        if (f.open(QIODevice::WriteOnly)) {
            QTextStream ts(&f);
            ts << "ID,图片路径,预测类别,置信度,模型类型,推理模式,时间\n";
            auto records = DatabaseManager::getInferenceRecords();
            for (const auto &r : records) {
                ts << r["id"].toString() << ","
                   << "\"" << r["image_path"].toString() << "\","
                   << r["predicted_class"].toString() << ","
                   << r["confidence"].toString() << ","
                   << r["model_type"].toString() << ","
                   << r["inference_mode"].toString() << ","
                   << r["created_at"].toString() << "\n";
            }
        }
    });
    connect(btnClose, &QPushButton::clicked, &dlg, &QDialog::accept);
    dlg.exec();
}

// ================= 撤销/重做 =================
void MainWindow::undoAnnotation()
{
    view->undo();
}

void MainWindow::redoAnnotation()
{
    view->redo();
}

void MainWindow::updateUndoRedoState()
{
    btnUndo->setEnabled(view->canUndo());
    btnRedo->setEnabled(view->canRedo());
}

// ================= 推理结果展示弹窗 =================
void MainWindow::showInferenceResultDialog(const QString &imgPath, const QString &resultText)
{
    QDialog dlg(this);
    dlg.setWindowTitle("推理结果");
    dlg.resize(700, 550);

    QVBoxLayout *layout = new QVBoxLayout(&dlg);
    layout->setSpacing(10);

    // 标题
    QLabel *titleLabel = new QLabel("🔍 表情识别推理结果");
    titleLabel->setStyleSheet("font-size:16px; font-weight:bold; color:#1976D2; padding:8px;");
    titleLabel->setAlignment(Qt::AlignCenter);
    layout->addWidget(titleLabel);

    // 图片显示
    QLabel *imgLabel = new QLabel();
    // 尝试加载 result 图片
    QFileInfo fi(imgPath);
    QString resultImgPath = fi.path() + "/" + fi.completeBaseName() + "_result.jpg";
    QPixmap resultPix;
    if (QFile::exists(resultImgPath)) {
        resultPix.load(resultImgPath);
    } else {
        resultPix.load(imgPath);
    }

    if (!resultPix.isNull()) {
        QPixmap scaled = resultPix.scaled(500, 350, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        imgLabel->setPixmap(scaled);
        imgLabel->setAlignment(Qt::AlignCenter);
        imgLabel->setStyleSheet("border:2px solid #BBDEFB; border-radius:8px; padding:4px; background:#FAFAFA;");
        layout->addWidget(imgLabel);
    }

    // 文字结果
    QLabel *textLabel = new QLabel(resultText);
    textLabel->setStyleSheet(
        "font-size:14px; padding:12px; border:2px solid #C8E6C9; "
        "border-radius:8px; background:#E8F5E9; color:#1B5E20;");
    textLabel->setWordWrap(true);
    textLabel->setAlignment(Qt::AlignCenter);
    layout->addWidget(textLabel);

    // 原始图片路径
    QLabel *pathLabel = new QLabel("原始图片: " + imgPath);
    pathLabel->setStyleSheet("font-size:10px; color:#888;");
    pathLabel->setWordWrap(true);
    layout->addWidget(pathLabel);

    QPushButton *btnClose = new QPushButton("关闭");
    btnClose->setMinimumHeight(32);
    btnClose->setStyleSheet(
        "QPushButton { background:#1976D2; color:white; border:none; border-radius:6px; font-size:13px; }"
        "QPushButton:hover { background:#1E88E5; }");
    layout->addWidget(btnClose);
    connect(btnClose, &QPushButton::clicked, &dlg, &QDialog::accept);

    dlg.exec();
}

// ================= 训练结果可视化对话框 =================
void MainWindow::showTrainingResultDialog(float mAP50, float mAP5095, const QString &modelPath)
{
    QDialog dlg(this);
    dlg.setWindowTitle("🎉 训练完成 - 性能报告");
    dlg.resize(500, 400);
    dlg.setStyleSheet("QDialog { background:#FAFAFA; }");

    QVBoxLayout *layout = new QVBoxLayout(&dlg);
    layout->setContentsMargins(16, 16, 16, 16);
    layout->setSpacing(12);

    // 标题
    QLabel *titleLabel = new QLabel("🎉 训练完成！");
    titleLabel->setStyleSheet("font-size:20px; font-weight:bold; color:#2E7D32;");
    layout->addWidget(titleLabel);

    // 性能指标卡片
    if (mAP50 > 0) {
        QGroupBox *metricsGroup = new QGroupBox("📊 性能指标");
        metricsGroup->setStyleSheet("QGroupBox { font-weight:bold; font-size:13px; padding-top:8px; }");
        QVBoxLayout *metricsLayout = new QVBoxLayout(metricsGroup);

        // mAP50
        QHBoxLayout *map50Row = new QHBoxLayout;
        QLabel *map50Label = new QLabel("mAP50:");
        map50Label->setMinimumWidth(80);
        map50Row->addWidget(map50Label);

        QProgressBar *map50Bar = new QProgressBar();
        map50Bar->setRange(0, 100);
        map50Bar->setValue(qRound(mAP50 * 100));
        map50Bar->setFormat(QString("%1%").arg(mAP50 * 100, 0, 'f', 1));
        map50Bar->setStyleSheet(
            "QProgressBar { border:1px solid #CCC; border-radius:4px; text-align:center; }"
            "QProgressBar::chunk { background:#4CAF50; border-radius:4px; }"
        );
        map50Bar->setMinimumHeight(20);
        map50Row->addWidget(map50Bar, 1);
        metricsLayout->addLayout(map50Row);

        // mAP50-95
        QHBoxLayout *map5095Row = new QHBoxLayout;
        QLabel *map5095Label = new QLabel("mAP50-95:");
        map5095Label->setMinimumWidth(80);
        map5095Row->addWidget(map5095Label);

        QProgressBar *map5095Bar = new QProgressBar();
        map5095Bar->setRange(0, 100);
        map5095Bar->setValue(qRound(mAP5095 * 100));
        map5095Bar->setFormat(QString("%1%").arg(mAP5095 * 100, 0, 'f', 1));
        map5095Bar->setStyleSheet(
            "QProgressBar { border:1px solid #CCC; border-radius:4px; text-align:center; }"
            "QProgressBar::chunk { background:#2196F3; border-radius:4px; }"
        );
        map5095Bar->setMinimumHeight(20);
        map5095Row->addWidget(map5095Bar, 1);
        metricsLayout->addLayout(map5095Row);

        layout->addWidget(metricsGroup);
    }

    // 模型信息卡片
    QGroupBox *modelGroup = new QGroupBox("📦 模型信息");
    modelGroup->setStyleSheet("QGroupBox { font-weight:bold; font-size:13px; padding-top:8px; }");
    QVBoxLayout *modelLayout = new QVBoxLayout(modelGroup);

    QLabel *modelPathLabel = new QLabel("模型路径: " + modelPath);
    modelPathLabel->setWordWrap(true);
    modelPathLabel->setStyleSheet("font-size:11px; color:#555;");
    modelLayout->addWidget(modelPathLabel);

    QFileInfo fi(modelPath);
    if (fi.exists()) {
        double sizeMB = fi.size() / (1024.0 * 1024.0);
        QLabel *sizeLabel = new QLabel(QString("模型大小: %1 MB").arg(sizeMB, 0, 'f', 1));
        sizeLabel->setStyleSheet("font-size:11px; color:#555;");
        modelLayout->addWidget(sizeLabel);
    }

    QLabel *configLabel = new QLabel(QString("训练配置: Epochs=%1, Batch=%2").arg(epochSpin->value()).arg(batchSpin->value()));
    configLabel->setStyleSheet("font-size:11px; color:#555;");
    modelLayout->addWidget(configLabel);

    layout->addWidget(modelGroup);

    // 按钮区
    QHBoxLayout *btnRow = new QHBoxLayout;
    btnRow->addStretch();

    QPushButton *btnViewModel = new QPushButton("📂 查看模型");
    btnViewModel->setMinimumHeight(32);
    btnViewModel->setStyleSheet(
        "QPushButton { background:#1976D2; color:white; border:none; border-radius:6px; font-size:12px; }"
        "QPushButton:hover { background:#1E88E5; }"
    );
    connect(btnViewModel, &QPushButton::clicked, [modelPath]() {
        QFileInfo fi(modelPath);
        QString dir = fi.exists() ? fi.absolutePath() : QFileInfo(modelPath).path();
        QDesktopServices::openUrl(QUrl::fromLocalFile(dir));
    });
    btnRow->addWidget(btnViewModel);

    QPushButton *btnClose = new QPushButton("关闭");
    btnClose->setMinimumHeight(32);
    btnClose->setStyleSheet(
        "QPushButton { background:#757575; color:white; border:none; border-radius:6px; font-size:12px; }"
        "QPushButton:hover { background:#9E9E9E; }"
    );
    connect(btnClose, &QPushButton::clicked, &dlg, &QDialog::accept);
    btnRow->addWidget(btnClose);

    layout->addLayout(btnRow);

    dlg.exec();
}
