#ifndef ANNOTATIONVIEW_H
#define ANNOTATIONVIEW_H

#include <QGraphicsView>
#include <QGraphicsRectItem>
#include <QMouseEvent>
#include <QMenu>
#include <QCursor>
#include <QStack>
#include <QKeyEvent>

// 自定义标注信息，附加到每个矩形框上
struct BoxInfo {
    int classId = 0;

    // 用于存储归一化的原始图像坐标（相对于原始图片尺寸）
    float normX = 0, normY = 0, normW = 0, normH = 0;
};

// ================= 撤销/重做命令 =================
struct UndoCommand {
    enum Type { AddBox, RemoveBox, MoveBox, ResizeBox, ChangeClass };
    Type type;
    int boxIndex;
    // 快照数据
    QRectF oldRect;      // 移动/缩放前的矩形
    QRectF newRect;      // 移动/缩放后的矩形
    int oldClass = 0;
    int newClass = 0;
    BoxInfo oldBoxInfo;  // RemoveBox 时保存的完整信息
};

// ================= 调整大小手柄方向 =================
enum class ResizeHandle {
    None,
    TopLeft,     Top,      TopRight,
    Left,                  Right,
    BottomLeft,  Bottom,   BottomRight
};

// 增强版标注视图：支持绘制、选中、移动、缩放编辑、撤销/重做
class AnnotationView : public QGraphicsView
{
    Q_OBJECT
public:
    explicit AnnotationView(QWidget *parent = nullptr);

    QList<QGraphicsRectItem*> boxes;
    QVector<BoxInfo> boxInfos;       // 每个框的附加信息

    // 缩放和平移功能
    void zoomIn();
    void zoomOut();
    void zoomFit();
    void resetView();

    // 设置当前图片尺寸（用于坐标转换）
    void setOriginalSize(const QSize &size);

    // 获取当前选中的框索引，-1表示无选中
    int selectedBoxIndex() const { return m_selectedIndex; }

    // 删除指定框
    void removeBox(int index);

    // 改变指定框的类别和颜色
    void setBoxClass(int index, int classId, const QColor &color);

    // 清空所有框
    void clearBoxes();

    // ===== 撤销/重做 =====
    void undo();
    void redo();
    bool canUndo() const { return !m_undoStack.isEmpty(); }
    bool canRedo() const { return !m_redoStack.isEmpty(); }
    void resetUndoStack();  // 切换图片时清空

    // ===== 框操作封装（带撤销记录） =====
    void addBoxWithUndo(const QRectF &rect, int classId, const BoxInfo &info);
    void removeBoxWithUndo(int index);
    void moveBoxWithUndo(int index, const QRectF &oldRect, const QRectF &newRect);
    void resizeBoxWithUndo(int index, const QRectF &oldRect, const QRectF &newRect);
    void changeClassWithUndo(int index, int oldClass, int newClass);

signals:
    void boxSelected(int index);     // 框被选中信号
    void boxMoved(int index);        // 框被移动信号
    void boxCreated();               // 新框被创建信号
    void boxChanged();               // 标注发生变更（用于撤销/重做状态更新）

protected:
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;   // 滚轮缩放
    void contextMenuEvent(QContextMenuEvent *event) override; // 右键菜单
    void keyPressEvent(QKeyEvent *event) override;   // 键盘快捷键
    void drawForeground(QPainter *painter, const QRectF &rect) override; // 绘制手柄

private:
    QPointF startPoint;
    QPoint lastPanPoint;            // 平移起始点
    QGraphicsRectItem *currentRect = nullptr;

    int m_selectedIndex = -1;       // 当前选中的框索引
    bool m_isDragging = false;      // 是否正在拖拽已有框
    bool m_isPanning = false;       // 是否正在平移视图
    bool m_isResizing = false;      // 是否正在调整框大小
    QPointF m_dragOffset;           // 拖拽偏移量
    QPointF m_resizeAnchor;         // 缩放时对角锚点
    QRectF m_dragStartRect;         // 拖动/缩放操作前的矩形

    ResizeHandle m_resizeHandle = ResizeHandle::None;  // 当前拖拽的手柄

    QSize m_originalSize;           // 原始图片尺寸
    QSize m_displaySize;            // 显示尺寸（缩放后）

    // ===== 撤销/重做栈 =====
    QStack<UndoCommand> m_undoStack;
    QStack<UndoCommand> m_redoStack;

    // ===== 辅助函数 =====
    QGraphicsRectItem *hitTest(const QPoint &pos);   // 点击检测是否命中某个框
    ResizeHandle hitTestHandle(const QPoint &pos, QGraphicsRectItem *box); // 检测手柄
    QColor getClassColor(int classId);               // 根据类别获取颜色

    void highlightSelectedBox();                      // 高亮选中框
    void drawResizeHandles(QPainter *painter);        // 绘制8个调整手柄
    void executeCommand(const UndoCommand &cmd);      // 执行命令（正向）
    void undoCommand(const UndoCommand &cmd);         // 撤销命令（反向）

    // 手柄检测阈值
    static constexpr int HANDLE_SIZE = 8;   // 手柄半边长
    static constexpr int EDGE_ZONE = 10;    // 边缘检测区域宽度
};

#endif // ANNOTATIONVIEW_H
