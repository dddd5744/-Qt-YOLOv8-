#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QGraphicsScene>
#include <QProcess>
#include <QLineEdit>
#include <QTextEdit>
#include <QComboBox>
#include <QProgressBar>
#include <QLabel>
#include <QListWidget>
#include <QSlider>
#include <QSpinBox>
#include <QCheckBox>
#include <QTableWidget>
#include <QMenuBar>
#include <QToolBar>
#include <QStatusBar>
#include <QActionGroup>
#include <QSplitter>
#include <QDialog>
#include <QTabWidget>
#include "AnnotationView.h"
#include "DatabaseManager.h"

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

private slots:
    // ---- 文件操作 ----
    void loadFolder();
    void openSingleImage();
    void exitApp();

    // ---- 图片浏览 ----
    void nextImage();
    void prevImage();
    void onImageSelected(int row);
    void onCurrentImageChanged(int idx);

    // ---- 标注操作 ----
    void deleteBox();
    void deleteSelectedBox();
    void changeBoxClass();
    void updateLabels();

    // ---- 撤销/重做 ----
    void undoAnnotation();
    void redoAnnotation();
    void updateUndoRedoState();

    // ---- 视图操作 ----
    void zoomIn();
    void zoomOut();
    void zoomFit();
    void resetView();
    void rotateLeft();
    void rotateRight();

    // ---- 训练流程 ----
    void startTraining();
    void readTrainingOutput();
    void onTrainingFinished(int exitCode, QProcess::ExitStatus status);
    void selectDatasetDir();
    void autoConvertToOnnx(const QString &ptPath);   // 训练后自动转ONNX

    // ---- 模型量化 ----
    void startQuantization();
    void selectOnnxModelForQuantize();
    void onQuantizationFinished(int exitCode, QProcess::ExitStatus status);

    // ---- 推理 ----
    void runInferencePython();
    void runInferenceCpp();
    void selectInferenceModel();
    void selectInferenceImages();
    void showInferenceResultDialog(const QString &imgPath, const QString &resultText);
    void showTrainingResultDialog(float mAP50, float mAP5095, const QString &modelPath);

    // ---- 数据库查看 ----
    void showTrainingHistory();
    void showInferenceHistory();
    void showDatasetInfo();              // 数据集概览

    // ---- 进程通用 ----
    void readOutput();
    void finished();

    // ---- Python环境 ----
    void checkPythonEnvironment();          // 检查Python和依赖
    void onPythonCheckFinished(int exitCode, QProcess::ExitStatus status);

private:
    // ===== UI 组件 =====
    AnnotationView *view;
    QGraphicsScene *scene;

    // 标注区控件
    QLineEdit *labelInput;
    QComboBox *labelBox;
    QPushButton *btnDeleteBox;
    QPushButton *btnUndo;
    QPushButton *btnRedo;

    // 图片列表
    QListWidget *imageListWidget;

    // 训练配置区
    QLineEdit *datasetEdit;
    QSpinBox *epochSpin;
    QSpinBox *batchSpin;
    QSlider *splitSlider;               // 训练集/验证集比例
    QLabel *splitLabel;
    QCheckBox *autoOnnxCheck;           // 训练后自动转ONNX
    QTextEdit *logOutput;
    QLabel *trainStatusLabel;
    QProgressBar *trainProgressBar;

    // 量化配置区
    QComboBox *quantTypeCombo;
    QLineEdit *onnxInputEdit;
    QPushButton *btnSelectOnnxInput;
    QProgressBar *quantProgressBar;
    QLabel *quantStatusLabel;

    // 推理区
    QLineEdit *inferenceModelEdit;
    QLineEdit *inferenceImageEdit;
    QPushButton *btnSelectInferenceModel;
    QPushButton *btnSelectInferenceImage;
    QComboBox *inferenceModeCombo;
    QLabel *inferenceResultLabel;
    QTableWidget *inferenceHistoryTable;

    // 菜单和工具栏
    QMenuBar *menuBar;
    QToolBar *toolBar;
    QStatusBar *statusBar;
    QSplitter *mainSplitter;           // 水平分割：标注区 | 控制区
    QSplitter *topBottomSplitter;      // 垂直分割：工作区 | 日志区

    // Python环境状态
    QLabel *pythonStatusLabel;         // 显示Python环境检测结果
    QLabel *listStatsLabel;            // 图片列表统计标签

    // ===== 数据管理 =====
    QStringList imageList;
    QString folderPath;
    int currentIndex;
    int currentRotation = 0;
    QPixmap originalPixmap;
    double currentZoomFactor = 1.0;

    // 类别名称列表
    QStringList classNames;

    // 进程控制
    QProcess *process;
    QString pythonExePath;
    bool pythonReady = false;          // Python环境是否可用
    QStringList pythonMissingModules;  // 缺失的Python模块
    QString accumulatedOutput;         // 累积的训练输出（用于解析指标）

    // 当前正在执行的任务类型（用于判断finished回调）
    enum ActiveTask { None, Training, Quantization, Inference, PythonCheck };
    ActiveTask activeTask = None;

    // ===== 方法 =====
    void setupUI();
    void setupMenuBar();
    void setupToolBar();
    void setupStatusBar();
    void connectSignals();

    // 标注逻辑
    void loadImage();
    void saveLabels();
    bool loadLabelsFromFile(const QString &imgName);
    void updateImageListDisplay();
    void refreshStatusBar();

    // 日志辅助
    void appendLog(const QString &text, const QColor &color = QColor());  // 带颜色的日志
    void appendErrorLog(const QString &text);    // 红色错误日志
    void appendSuccessLog(const QString &text);  // 绿色成功日志

    // C++ 推理 (OpenCV DNN)
    bool doCppInference(const QString &imgPath, const QString &modelPath);

    // 工具函数
    QString getScriptPath(const QString &scriptName);
    QString getModelPath(const QString &modelName);
    bool findPythonWithUltralytics();              // 查找含ultralytics的Python
};

#endif // MAINWINDOW_H
