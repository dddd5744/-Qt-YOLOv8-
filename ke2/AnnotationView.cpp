#include "AnnotationView.h"
#include <QPen>
#include <QBrush>
#include <QPainter>
#include <QtMath>
#include <QWidget>
#include <QKeyEvent>

// 类别颜色映射（默认10种颜色循环）
static const QVector<QColor> CLASS_COLORS = {
    Qt::red, Qt::green, Qt::blue, QColor(255,165,0), Qt::cyan,
    Qt::magenta, QColor(128,0,128), QColor(0,128,128), QColor(128,128,0), QColor(0,0,128)
};

AnnotationView::AnnotationView(QWidget *parent)
    : QGraphicsView(parent)
{
    setRenderHints(QPainter::Antialiasing | QPainter::SmoothPixmapTransform);
    setMouseTracking(true);
    setDragMode(NoDrag);
    setSceneRect(-10000, -10000, 20000, 20000);
}

// ================= 原始图尺寸 =================
void AnnotationView::setOriginalSize(const QSize &size)
{
    m_originalSize = size;
}

// ================= 框管理 =================
void AnnotationView::clearBoxes()
{
    // ⚠️ 先清理临时绘制框（如果用户正在绘制中切换图片）
    if (currentRect) {
        if (scene()) scene()->removeItem(currentRect);
        delete currentRect;
        currentRect = nullptr;
    }

    for (auto *box : boxes) {
        if (box && scene())
            scene()->removeItem(box);
        delete box;
    }
    boxes.clear();
    boxInfos.clear();

    // 重置所有交互状态
    m_selectedIndex = -1;
    m_isDragging = false;
    m_isResizing = false;
    m_isPanning = false;
    m_resizeHandle = ResizeHandle::None;
    m_dragStartRect = QRectF();

    resetUndoStack();
    viewport()->update();
}

void AnnotationView::removeBox(int index)
{
    if (index < 0 || index >= boxes.size()) return;
    auto *box = boxes.takeAt(index);
    if (scene()) scene()->removeItem(box);
    delete box;
    boxInfos.removeAt(index);
    if (m_selectedIndex == index) m_selectedIndex = -1;
    else if (m_selectedIndex > index) m_selectedIndex--;
}

void AnnotationView::setBoxClass(int index, int classId, const QColor &color)
{
    if (index < 0 || index >= boxes.size()) return;
    boxInfos[index].classId = classId;
    boxes[index]->setPen(QPen(color, 2));
}

QColor AnnotationView::getClassColor(int classId)
{
    return CLASS_COLORS[classId % CLASS_COLORS.size()];
}

// ================= 高亮选中框 =================
void AnnotationView::highlightSelectedBox()
{
    for (int i = 0; i < boxes.size(); ++i) {
        if (i == m_selectedIndex) {
            boxes[i]->setPen(QPen(Qt::white, 3));
        } else {
            int cls = boxInfos[i].classId;
            boxes[i]->setPen(QPen(getClassColor(cls), 2));
        }
    }
    if (scene()) scene()->update();  // 强制刷新消除残留
}

// ================= 撤销/重做核心实现 =================
void AnnotationView::resetUndoStack()
{
    m_undoStack.clear();
    m_redoStack.clear();
    emit boxChanged();
}

void AnnotationView::executeCommand(const UndoCommand &cmd)
{
    switch (cmd.type) {
    case UndoCommand::AddBox: {
        QGraphicsRectItem *rect = new QGraphicsRectItem(cmd.newRect);
        rect->setPen(QPen(getClassColor(cmd.newClass), 2));
        rect->setBrush(QBrush(QColor(getClassColor(cmd.newClass).red(),
                                     getClassColor(cmd.newClass).green(),
                                     getClassColor(cmd.newClass).blue(), 30)));
        scene()->addItem(rect);
        boxes.append(rect);
        BoxInfo info;
        info.classId = cmd.newClass;
        if (m_originalSize.width() > 0 && m_originalSize.height() > 0) {
            info.normX = (float)(cmd.newRect.x() / m_originalSize.width());
            info.normY = (float)(cmd.newRect.y() / m_originalSize.height());
            info.normW = (float)(cmd.newRect.width() / m_originalSize.width());
            info.normH = (float)(cmd.newRect.height() / m_originalSize.height());
        }
        boxInfos.append(info);
        if (scene()) scene()->update();
        break;
    }
    case UndoCommand::RemoveBox:
        removeBox(cmd.boxIndex);
        if (scene()) scene()->update();
        break;
    case UndoCommand::MoveBox:
    case UndoCommand::ResizeBox:
        if (cmd.boxIndex >= 0 && cmd.boxIndex < boxes.size())
            boxes[cmd.boxIndex]->setRect(cmd.newRect);
        break;
    case UndoCommand::ChangeClass:
        if (cmd.boxIndex >= 0 && cmd.boxIndex < boxes.size()) {
            boxInfos[cmd.boxIndex].classId = cmd.newClass;
            boxes[cmd.boxIndex]->setPen(QPen(getClassColor(cmd.newClass), 2));
        }
        break;
    }
}

void AnnotationView::undoCommand(const UndoCommand &cmd)
{
    switch (cmd.type) {
    case UndoCommand::AddBox:
        removeBox(boxes.size() - 1);
        if (scene()) scene()->update();
        break;
    case UndoCommand::RemoveBox: {
        QGraphicsRectItem *rect = new QGraphicsRectItem(cmd.oldRect);
        rect->setPen(QPen(getClassColor(cmd.oldClass), 2));
        rect->setBrush(QBrush(QColor(getClassColor(cmd.oldClass).red(),
                                     getClassColor(cmd.oldClass).green(),
                                     getClassColor(cmd.oldClass).blue(), 30)));
        scene()->addItem(rect);
        boxes.insert(cmd.boxIndex, rect);
        boxInfos.insert(cmd.boxIndex, cmd.oldBoxInfo);
        if (scene()) scene()->update();
        break;
    }
    case UndoCommand::MoveBox:
    case UndoCommand::ResizeBox:
        if (cmd.boxIndex >= 0 && cmd.boxIndex < boxes.size())
            boxes[cmd.boxIndex]->setRect(cmd.oldRect);
        break;
    case UndoCommand::ChangeClass:
        if (cmd.boxIndex >= 0 && cmd.boxIndex < boxes.size()) {
            boxInfos[cmd.boxIndex].classId = cmd.oldClass;
            boxes[cmd.boxIndex]->setPen(QPen(getClassColor(cmd.oldClass), 2));
        }
        break;
    }
}

void AnnotationView::undo()
{
    if (m_undoStack.isEmpty()) return;
    UndoCommand cmd = m_undoStack.pop();
    m_redoStack.push(cmd);
    undoCommand(cmd);
    if (m_selectedIndex >= boxes.size()) m_selectedIndex = -1;
    highlightSelectedBox();
    if (scene()) scene()->update();
    emit boxChanged();
}

void AnnotationView::redo()
{
    if (m_redoStack.isEmpty()) return;
    UndoCommand cmd = m_redoStack.pop();
    m_undoStack.push(cmd);
    executeCommand(cmd);
    if (m_selectedIndex >= boxes.size()) m_selectedIndex = -1;
    highlightSelectedBox();
    if (scene()) scene()->update();
    emit boxChanged();
}

// ===== 带撤销的封装操作 =====
void AnnotationView::addBoxWithUndo(const QRectF &rect, int classId, const BoxInfo &info)
{
    UndoCommand cmd;
    cmd.type = UndoCommand::AddBox;
    cmd.boxIndex = boxes.size();
    cmd.newRect = rect;
    cmd.newClass = classId;
    m_undoStack.push(cmd);
    m_redoStack.clear();

    QGraphicsRectItem *rectItem = new QGraphicsRectItem(rect);
    rectItem->setPen(QPen(getClassColor(classId), 2));
    rectItem->setBrush(QBrush(QColor(getClassColor(classId).red(),
                                     getClassColor(classId).green(),
                                     getClassColor(classId).blue(), 30)));
    scene()->addItem(rectItem);
    boxes.append(rectItem);
    boxInfos.append(info);

    // 取消之前选中，选中新框
    m_selectedIndex = boxes.size() - 1;
    highlightSelectedBox();

    if (scene()) scene()->update();
    emit boxChanged();
    emit boxCreated();
}

void AnnotationView::removeBoxWithUndo(int index)
{
    if (index < 0 || index >= boxes.size()) return;
    UndoCommand cmd;
    cmd.type = UndoCommand::RemoveBox;
    cmd.boxIndex = index;
    cmd.oldRect = boxes[index]->rect();
    cmd.oldClass = boxInfos[index].classId;
    cmd.oldBoxInfo = boxInfos[index];
    m_undoStack.push(cmd);
    m_redoStack.clear();

    removeBox(index);
    if (scene()) scene()->update();  // 强制刷新消除残留
    emit boxChanged();
}

void AnnotationView::moveBoxWithUndo(int index, const QRectF &oldRect, const QRectF &newRect)
{
    if (index < 0 || index >= boxes.size()) return;
    UndoCommand cmd;
    cmd.type = UndoCommand::MoveBox;
    cmd.boxIndex = index;
    cmd.oldRect = oldRect;
    cmd.newRect = newRect;
    m_undoStack.push(cmd);
    m_redoStack.clear();
    emit boxChanged();
}

void AnnotationView::resizeBoxWithUndo(int index, const QRectF &oldRect, const QRectF &newRect)
{
    if (index < 0 || index >= boxes.size()) return;
    UndoCommand cmd;
    cmd.type = UndoCommand::ResizeBox;
    cmd.boxIndex = index;
    cmd.oldRect = oldRect;
    cmd.newRect = newRect;
    m_undoStack.push(cmd);
    m_redoStack.clear();
    emit boxChanged();
}

void AnnotationView::changeClassWithUndo(int index, int oldClass, int newClass)
{
    if (index < 0 || index >= boxes.size()) return;
    UndoCommand cmd;
    cmd.type = UndoCommand::ChangeClass;
    cmd.boxIndex = index;
    cmd.oldClass = oldClass;
    cmd.newClass = newClass;
    m_undoStack.push(cmd);
    m_redoStack.clear();

    boxInfos[index].classId = newClass;
    boxes[index]->setPen(QPen(getClassColor(newClass), 2));
    if (scene()) scene()->update();
    emit boxChanged();
}

// ================= 手柄检测 =================
ResizeHandle AnnotationView::hitTestHandle(const QPoint &pos, QGraphicsRectItem *box)
{
    if (!box) return ResizeHandle::None;
    QPointF scenePos = mapToScene(pos);
    QRectF r = box->rect();
    qreal x = scenePos.x(), y = scenePos.y();

    auto near = [](qreal a, qreal b) { return qAbs(a - b) <= HANDLE_SIZE; };

    // 四角
    if (near(x, r.left()) && near(y, r.top()))     return ResizeHandle::TopLeft;
    if (near(x, r.right()) && near(y, r.top()))    return ResizeHandle::TopRight;
    if (near(x, r.left()) && near(y, r.bottom()))  return ResizeHandle::BottomLeft;
    if (near(x, r.right()) && near(y, r.bottom())) return ResizeHandle::BottomRight;

    // 四边中点
    if (near(y, r.top()) && x > r.left() + HANDLE_SIZE && x < r.right() - HANDLE_SIZE)
        return ResizeHandle::Top;
    if (near(y, r.bottom()) && x > r.left() + HANDLE_SIZE && x < r.right() - HANDLE_SIZE)
        return ResizeHandle::Bottom;
    if (near(x, r.left()) && y > r.top() + HANDLE_SIZE && y < r.bottom() - HANDLE_SIZE)
        return ResizeHandle::Left;
    if (near(x, r.right()) && y > r.top() + HANDLE_SIZE && y < r.bottom() - HANDLE_SIZE)
        return ResizeHandle::Right;

    return ResizeHandle::None;
}

// ================= 绘制调整手柄 =================
void AnnotationView::drawResizeHandles(QPainter *painter)
{
    if (m_selectedIndex < 0 || m_selectedIndex >= boxes.size()) return;

    QRectF r = boxes[m_selectedIndex]->rect();
    QVector<QPointF> handlePoints = {
        r.topLeft(),     QPointF(r.center().x(), r.top()),
        r.topRight(),    QPointF(r.right(), r.center().y()),
        r.bottomRight(), QPointF(r.center().x(), r.bottom()),
        r.bottomLeft(),  QPointF(r.left(), r.center().y()),
    };

    painter->save();
    painter->setPen(QPen(Qt::white, 1));
    painter->setBrush(QBrush(QColor(25, 118, 210))); // 蓝色填充

    for (const auto &pt : handlePoints) {
        painter->drawRect(QRectF(pt.x() - HANDLE_SIZE, pt.y() - HANDLE_SIZE,
                                  HANDLE_SIZE * 2, HANDLE_SIZE * 2));
    }
    painter->restore();
}

// ================= 重写 paintEvent 以绘制手柄 =================
void AnnotationView::drawForeground(QPainter *painter, const QRectF &rect)
{
    Q_UNUSED(rect);
    drawResizeHandles(painter);
}

// ================= 鼠标事件 =================
void AnnotationView::mousePressEvent(QMouseEvent *event)
{
    if (!scene()) return;

    // 中键或 Ctrl+左键: 开始平移
    if (event->button() == Qt::MiddleButton ||
        (event->button() == Qt::LeftButton && event->modifiers() & Qt::ControlModifier)) {
        m_isPanning = true;
        lastPanPoint = event->pos();
        setCursor(Qt::ClosedHandCursor);
        event->accept();
        return;
    }

    if (event->button() == Qt::LeftButton) {
        // 先检查是否命中已有框的手柄（仅在已有选中框时）
        if (m_selectedIndex >= 0 && m_selectedIndex < boxes.size()) {
            m_resizeHandle = hitTestHandle(event->pos(), boxes[m_selectedIndex]);
            if (m_resizeHandle != ResizeHandle::None) {
                m_isResizing = true;
                m_resizeAnchor = mapToScene(event->pos());
                event->accept();
                return;
            }
        }

        // 检测是否点击了已有框
        QGraphicsRectItem *hit = hitTest(event->pos());

        if (hit) {
            int idx = boxes.indexOf(hit);
            if (idx >= 0) {
                // 取消之前的选中高亮
                if (m_selectedIndex >= 0 && m_selectedIndex < boxes.size()
                    && m_selectedIndex != idx) {
                    highlightSelectedBox();
                }

                m_selectedIndex = idx;
                m_isDragging = true;
                startPoint = mapToScene(event->pos());
                m_dragOffset = startPoint - hit->rect().topLeft();
                m_dragStartRect = hit->rect();  // 记录拖动前的矩形

                highlightSelectedBox();
                // 强制刷新以显示手柄
                viewport()->update();

                emit boxSelected(idx);
            }
            event->accept();
            return;
        }

        // 没有命中任何框 → 取消选中
        if (m_selectedIndex >= 0) {
            m_selectedIndex = -1;
            highlightSelectedBox();
            viewport()->update();
            emit boxSelected(-1);
        }

        // 绘制新框
        startPoint = mapToScene(event->pos());
        currentRect = new QGraphicsRectItem();
        currentRect->setPen(QPen(Qt::red, 2));
        currentRect->setBrush(QBrush(QColor(255, 0, 0, 30)));
        scene()->addItem(currentRect);
        event->accept();
    }

    QGraphicsView::mousePressEvent(event);
}

void AnnotationView::mouseMoveEvent(QMouseEvent *event)
{
    // 平移中
    if (m_isPanning && (event->buttons() & Qt::MiddleButton ||
                        (event->buttons() & Qt::LeftButton && event->modifiers() & Qt::ControlModifier))) {
        QPointF delta = mapToScene(event->pos()) - mapToScene(lastPanPoint);
        lastPanPoint = event->pos();
        QTransform t = transform();
        t.translate(delta.x(), delta.y());
        setTransform(t);
        event->accept();
        return;
    }

    // 缩放中
    if (m_isResizing && m_selectedIndex >= 0 && m_selectedIndex < boxes.size()) {
        QPointF currentPos = mapToScene(event->pos());
        QRectF r = boxes[m_selectedIndex]->rect();
        QRectF newRect = r;

        switch (m_resizeHandle) {
        case ResizeHandle::TopLeft:
            newRect.setTopLeft(currentPos);
            break;
        case ResizeHandle::TopRight:
            newRect.setTopRight(currentPos);
            break;
        case ResizeHandle::BottomLeft:
            newRect.setBottomLeft(currentPos);
            break;
        case ResizeHandle::BottomRight:
            newRect.setBottomRight(currentPos);
            break;
        case ResizeHandle::Top:
            newRect.setTop(currentPos.y());
            break;
        case ResizeHandle::Bottom:
            newRect.setBottom(currentPos.y());
            break;
        case ResizeHandle::Left:
            newRect.setLeft(currentPos.x());
            break;
        case ResizeHandle::Right:
            newRect.setRight(currentPos.x());
            break;
        default:
            break;
        }

        // 确保宽高为正
        if (newRect.width() < 5) newRect.setWidth(5);
        if (newRect.height() < 5) newRect.setHeight(5);

        boxes[m_selectedIndex]->setRect(newRect);
        event->accept();
        return;
    }

    // 拖拽已有框
    if (m_isDragging && !boxes.isEmpty() && m_selectedIndex >= 0) {
        QPointF newPos = mapToScene(event->pos()) - m_dragOffset;
        auto *selectedBox = boxes[m_selectedIndex];
        QRectF rect = selectedBox->rect();
        rect.moveTo(newPos);
        selectedBox->setRect(rect);
        emit boxMoved(m_selectedIndex);
        event->accept();
        return;
    }

    // 绘制新框
    if (currentRect && (event->buttons() & Qt::LeftButton)) {
        QPointF end = mapToScene(event->pos());
        QRectF rect = QRectF(startPoint, end).normalized();
        currentRect->setRect(rect);
        event->accept();
        return;
    }

    // 更新光标样式（悬停在手柄/框上时）
    if (m_selectedIndex >= 0 && m_selectedIndex < boxes.size() && !m_isDragging && !m_isResizing) {
        ResizeHandle h = hitTestHandle(event->pos(), boxes[m_selectedIndex]);
        switch (h) {
        case ResizeHandle::TopLeft: case ResizeHandle::BottomRight:
            setCursor(Qt::SizeFDiagCursor); break;
        case ResizeHandle::TopRight: case ResizeHandle::BottomLeft:
            setCursor(Qt::SizeBDiagCursor); break;
        case ResizeHandle::Top: case ResizeHandle::Bottom:
            setCursor(Qt::SizeVerCursor); break;
        case ResizeHandle::Left: case ResizeHandle::Right:
            setCursor(Qt::SizeHorCursor); break;
        default: {
            QGraphicsRectItem *hit = hitTest(event->pos());
            setCursor(hit ? Qt::OpenHandCursor : Qt::ArrowCursor);
        }
        }
    }

    QGraphicsView::mouseMoveEvent(event);
}

void AnnotationView::mouseReleaseEvent(QMouseEvent *event)
{
    // 结束平移
    if (m_isPanning) {
        m_isPanning = false;
        setCursor(Qt::ArrowCursor);
        event->accept();
        return;
    }

    // 结束缩放（记录撤销）
    if (m_isResizing && m_selectedIndex >= 0 && m_selectedIndex < boxes.size()) {
        QRectF currentRect = boxes[m_selectedIndex]->rect();
        // 只有当真正发生变化时才记录
        if (currentRect != m_dragStartRect && m_dragStartRect.isValid()) {
            resizeBoxWithUndo(m_selectedIndex, m_dragStartRect, currentRect);
        }
        m_isResizing = false;
        m_resizeHandle = ResizeHandle::None;
        m_dragStartRect = QRectF();
        setCursor(Qt::ArrowCursor);
        highlightSelectedBox();
        if (scene()) scene()->update();
        event->accept();
        return;
    }

    // 结束拖拽（记录撤销）
    if (m_isDragging && m_selectedIndex >= 0 && m_selectedIndex < boxes.size()) {
        QRectF currentRect = boxes[m_selectedIndex]->rect();
        if (currentRect != m_dragStartRect && m_dragStartRect.isValid()) {
            moveBoxWithUndo(m_selectedIndex, m_dragStartRect, currentRect);
        }
        m_isDragging = false;
        m_dragStartRect = QRectF();
        highlightSelectedBox();
        if (scene()) scene()->update();
        event->accept();
        return;
    }

    // 完成新框绘制
    if (currentRect) {
        QRectF finalRect = currentRect->rect().normalized();

        // ⚠️ 关键：先移除临时框！否则会留在场景中造成残留红线
        if (scene()) {
            scene()->removeItem(currentRect);
            scene()->update();
        }
        delete currentRect;
        currentRect = nullptr;

        if (finalRect.width() > 5 && finalRect.height() > 5) {
            int classId = 0;
            BoxInfo info;
            info.classId = classId;
            if (m_originalSize.width() > 0 && m_originalSize.height() > 0) {
                info.normX = (float)(finalRect.x() / m_originalSize.width());
                info.normY = (float)(finalRect.y() / m_originalSize.height());
                info.normW = (float)(finalRect.width() / m_originalSize.width());
                info.normH = (float)(finalRect.height() / m_originalSize.height());
            }
            addBoxWithUndo(finalRect, classId, info);
            // addBoxWithUndo 内部已 emit boxCreated() + boxChanged()，此处不重复
        }
        // 框太小则静默丢弃
        event->accept();
        return;
    }

    QGraphicsView::mouseReleaseEvent(event);
}

// ================= 键盘快捷键 =================
void AnnotationView::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Delete || event->key() == Qt::Key_Backspace) {
        if (m_selectedIndex >= 0 && m_selectedIndex < boxes.size()) {
            removeBoxWithUndo(m_selectedIndex);
            m_selectedIndex = -1;
            emit boxSelected(-1);
            event->accept();
            return;
        }
    }

    if (event->matches(QKeySequence::Undo)) {
        undo();
        event->accept();
        return;
    }
    if (event->matches(QKeySequence::Redo)) {
        redo();
        event->accept();
        return;
    }

    QGraphicsView::keyPressEvent(event);
}

// ================= 滚轮缩放 =================
void AnnotationView::wheelEvent(QWheelEvent *event)
{
    if (event->modifiers() & Qt::ControlModifier) {
        double factor = event->angleDelta().y() > 0 ? 1.15 : 0.87;
        scale(factor, factor);
        event->accept();
        return;
    }
    QGraphicsView::wheelEvent(event);
}

void AnnotationView::zoomIn() { scale(1.25, 1.25); }
void AnnotationView::zoomOut() { scale(0.8, 0.8); }
void AnnotationView::zoomFit() {
    if (scene() && !scene()->items().isEmpty()) {
        fitInView(scene()->itemsBoundingRect(), Qt::KeepAspectRatio);
    }
}
void AnnotationView::resetView() { resetTransform(); }

// ================= 右键菜单 =================
void AnnotationView::contextMenuEvent(QContextMenuEvent *event)
{
    QGraphicsRectItem *hit = hitTest(event->pos());
    if (!hit) return;

    QMenu menu(this);
    QAction *actDelete = menu.addAction("删除此标注框");
    QAction *selected = menu.exec(event->globalPos());

    if (selected == actDelete) {
        int idx = boxes.indexOf(hit);
        if (idx >= 0) {
            removeBoxWithUndo(idx);
            m_selectedIndex = -1;
            emit boxSelected(-1);
            if (scene()) scene()->update();
        }
    }
}

// ================= 辅助函数 =================
QGraphicsRectItem* AnnotationView::hitTest(const QPoint &pos)
{
    QPointF scenePos = mapToScene(pos);
    for (int i = boxes.size() - 1; i >= 0; --i) {
        if (boxes[i] && boxes[i]->rect().contains(scenePos))
            return boxes[i];
    }
    return nullptr;
}
