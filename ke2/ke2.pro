QT       += widgets sql

CONFIG += c++17

# ----------------------------------------------------
# 项目基础文件
# ----------------------------------------------------
SOURCES += \
    AnnotationView.cpp \
    main.cpp \
    mainwindow.cpp

HEADERS += \
    AnnotationView.h \
    DatabaseManager.h \
    mainwindow.h

FORMS += \
    mainwindow.ui

# ----------------------------------------------------
# OpenCV 4 配置 (Mac Apple Silicon)
# ----------------------------------------------------
INCLUDEPATH += /opt/homebrew/include/opencv4

LIBS += -L/opt/homebrew/lib \
        -lopencv_core \
        -lopencv_imgproc \
        -lopencv_imgcodecs \
        -lopencv_dnn

# ----------------------------------------------------
# 部署
# ----------------------------------------------------
qnx: target.path = /tmp/$${TARGET}/bin
else: unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS = target

DISTFILES += \
    train.py \
    quantize.py \
    inference.py \
    convert_onnx.py
