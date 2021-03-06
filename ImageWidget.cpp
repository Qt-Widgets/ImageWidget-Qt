﻿// UTF-8 with BOM

// Avoid gibberish when use MSVC
#if _MSC_VER >= 1600
#pragma execution_character_set("utf-8")
#endif

#include <QMouseEvent>
#include "ImageWidget.h"
#include <QFileDialog>
#include <QCoreApplication>
#include <QMessageBox>
#include <QPainter>

SelectRect::SelectRect(QWidget * parent) : QWidget(parent)
{
    mouseStatus = Qt::NoButton;

    // 初始化右键菜单
    mMenu = new QMenu(this);
    mActionReset = mMenu->addAction(tr("重选"));// Reset
    mActionSaveZoomedImage = mMenu->addAction(tr("另存为(缩放图像)"));// Save as (From the zoomed image)
    mActionSaveOriginalImage = mMenu->addAction(tr("另存为(实际图像)"));// Save as (From the original image)
    mActionExit = mMenu->addAction(tr("退出"));// Exit

    connect(mActionExit, SIGNAL(triggered()), this, SLOT(selectExit()));
    connect(mActionSaveZoomedImage, SIGNAL(triggered()), this, SLOT(cropZoomedImage()));
    connect(mActionSaveOriginalImage, SIGNAL(triggered()), this, SLOT(cropOriginalImage()));
    connect(mActionReset, SIGNAL(triggered()), this, SLOT(selectReset()));
    // 关闭后释放资源
    this->setAttribute(Qt::WA_DeleteOnClose);
    this->setFocusPolicy(Qt::StrongFocus);
}

SelectRect::~SelectRect()
{
    image = nullptr;
    zoomedImage = nullptr;
    mMenu = nullptr;
}

void SelectRect::receiveParentSizeChangedSignal()
{
    ImageWidget* parentWidget = static_cast<ImageWidget*>(this->parent());
    this->setGeometry(0, 0, parentWidget->width(), parentWidget->height());
    //    qDebug() << this->geometry();
    drawImageTopLeftPos = parentWidget->getDrawImageTopLeftPos();
    update();
}

void SelectRect::paintEvent(QPaintEvent* event)
{
    // 背景
    QPainterPath mask;
    // 选中的范围
    QPainterPath selectArea;
    // 椭圆
    selectArea.addRect(selectedRect[SR_CENTER].x(), selectedRect[SR_CENTER].y(), selectedRect[SR_CENTER].width(), selectedRect[SR_CENTER].height());
    mask.addRect(this->geometry());
    QPainterPath drawMask = mask.subtracted(selectArea);
    QPainter painter(this);
    painter.setPen(QPen(QColor(255, 0, 0, 255), 1));
    painter.fillPath(drawMask, QBrush(QColor(0, 0, 0, 160)));
    painter.drawRect(selectedRect[SR_CENTER]);
    if (isSelectedRectStable)
    {
        painter.setPen(QPen(QColor(0, 140, 255, 255), 1));
        painter.drawRect(selectedRect[SR_CENTER]);
        for (int i = 1; i < 5; i++)
            painter.fillRect(selectedRect[i], QBrush(QColor(0, 140, 255, 255)));
    }
}

void SelectRect::mousePressEvent(QMouseEvent* event)
{
    switch (event->button())
    {
    case Qt::LeftButton:
        mouseStatus = Qt::LeftButton;
        mouseLeftClickedPos = event->pos();
        // 关闭鼠标追踪 节省资源
        this->setMouseTracking(false);
        break;
    case Qt::RightButton:
        mouseStatus = Qt::RightButton;
        break;
    case Qt::MiddleButton:
        mouseStatus = Qt::MiddleButton;
        break;
    default:
        mouseStatus = Qt::NoButton;
    }
}

void SelectRect::mouseMoveEvent(QMouseEvent* event)
{
    if (mouseStatus == Qt::LeftButton)
    {
        isSelectedRectStable = false;
        isSelectedRectExisted = true;
        selectedRectChangeEvent(cursorPosInSelectedArea, event->pos());
        update();
    }
    // 判断鼠标是否在矩形框内
    if (mouseStatus == Qt::NoButton && isSelectedRectStable)
    {
        if (selectedRect[SR_ENTIRETY].contains(event->pos()))
        {
            cursorPosInSelectedArea = getSelectedAreaSubscript(event->pos());
        }
        else
        {
            cursorPosInSelectedArea = SR_NULL;
            this->setCursor(Qt::ArrowCursor);
        }
    }
}

void SelectRect::mouseReleaseEvent(QMouseEvent* event)
{
    if (mouseStatus == Qt::LeftButton)
    {
        // 修正RectInfo::w和RectInfo::h为正
        fixRectInfo(selectedRect[SR_CENTER]);
        // 备份
        lastSelectedRect = selectedRect[SR_CENTER];
        mouseStatus = Qt::NoButton;
        getEdgeRect();
        isSelectedRectStable = true;
        isSelectedRectExisted = true;
        update();
        // 开启鼠标追踪
        this->setMouseTracking(true);
        mouseStatus = Qt::NoButton;
    }
}

void SelectRect::contextMenuEvent(QContextMenuEvent* event)
{
    mMenu->exec(QCursor::pos());
    mouseStatus = Qt::NoButton;
}

void SelectRect::wheelEvent(QWheelEvent* event)
{
    eventFilter(this, event);
}

bool SelectRect::eventFilter(QObject* watched, QEvent* event)
{
    if (event->type() == QEvent::KeyPress)
    {
        QKeyEvent* keyEvent = static_cast<QKeyEvent*>(event);
        if (keyEvent->key() == Qt::Key_Escape)
        {
            // 如果存在选中框则删除选中框 不存在则退出
            if (isSelectedRectExisted)
            {
                selectReset();
                return true;
            }
            else
                this->selectExit();
        }
    }
    // 截断wheelEvent
    if (event->type() == QEvent::Wheel)
    {
        QWheelEvent* wheelEvent = static_cast<QWheelEvent*>(event);
        wheelEvent->accept();
        return true;
    }
    return false;
}

void SelectRect::keyPressEvent(QKeyEvent* event)
{
    eventFilter(this, event);
}

QRect SelectRect::getRectInImage(const QImage* img, const QPoint& imgTopLeftPos, QRect rect)
{
    QRect returnRect;
    // 计算相对于图像内的坐标
    returnRect.setTopLeft(rect.topLeft() - imgTopLeftPos);
    returnRect.setSize(rect.size());
    // 限定截取范围在图像内 修正顶点
    if (returnRect.x() < 0)
    {
        // QRect::setX change the width
        returnRect.setX(0);
    }
    if (returnRect.y() < 0)
    {
        // QRect::setY change the height
        returnRect.setY(0);
    }
    if (returnRect.bottomRight().x() >= img->width())
        // QRect::setRight change the width
        returnRect.setRight(img->width() - 1);
    if (returnRect.bottomRight().y() >= img->height())
        // Qrect::setBottom change the height
        returnRect.setBottom(img->height() - 1);
    return returnRect;
}

void SelectRect::getEdgeRect()
{
    // 边框宽
    int w = 5;

    selectedRect[SR_TOPLEFT].setTopLeft(selectedRect[SR_CENTER].topLeft() + QPoint(-w, -w));
    selectedRect[SR_TOPLEFT].setBottomRight(selectedRect[SR_CENTER].topLeft() + QPoint(-1, -1));

    selectedRect[SR_TOPRIGHT].setTopRight(selectedRect[SR_CENTER].topRight() + QPoint(w, -w));
    selectedRect[SR_TOPRIGHT].setBottomLeft(selectedRect[SR_CENTER].topRight() + QPoint(1, -1));

    selectedRect[SR_BOTTOMRIGHT].setTopLeft(selectedRect[SR_CENTER].bottomRight() + QPoint(1, 1));
    selectedRect[SR_BOTTOMRIGHT].setBottomRight(selectedRect[SR_CENTER].bottomRight() + QPoint(w, w));

    selectedRect[SR_BOTTOMLEFT].setTopRight(selectedRect[SR_CENTER].bottomLeft() + QPoint(-1, 1));
    selectedRect[SR_BOTTOMLEFT].setBottomLeft(selectedRect[SR_CENTER].bottomLeft() + QPoint(-w, w));

    selectedRect[SR_TOP].setTopLeft(selectedRect[SR_TOPLEFT].topRight() + QPoint(1, 0));
    selectedRect[SR_TOP].setBottomRight(selectedRect[SR_TOPRIGHT].bottomLeft() + QPoint(-1, 0));

    selectedRect[SR_RIGHT].setTopLeft(selectedRect[SR_TOPRIGHT].bottomLeft() + QPoint(0, 1));
    selectedRect[SR_RIGHT].setBottomRight(selectedRect[SR_BOTTOMRIGHT].topRight() + QPoint(0, -1));

    selectedRect[SR_BOTTOM].setTopLeft(selectedRect[SR_BOTTOMLEFT].topRight() + QPoint(1, 0));
    selectedRect[SR_BOTTOM].setBottomRight(selectedRect[SR_BOTTOMRIGHT].bottomLeft() + QPoint(-1, 0));

    selectedRect[SR_LEFT].setTopLeft(selectedRect[SR_TOPLEFT].bottomLeft() + QPoint(0, 1));
    selectedRect[SR_LEFT].setBottomRight(selectedRect[SR_BOTTOMLEFT].topRight() + QPoint(0, -1));

    selectedRect[SR_ENTIRETY].setTopLeft(selectedRect[SR_TOPLEFT].topLeft());
    selectedRect[SR_ENTIRETY].setBottomRight(selectedRect[SR_BOTTOMRIGHT].bottomRight());
}

int SelectRect::getSelectedAreaSubscript(QPoint cursorPos)
{
    // 可用树结构减少if
    if (selectedRect[SR_CENTER].contains(cursorPos))
    {
        this->setCursor(Qt::SizeAllCursor);
        return SR_CENTER;
    }
    if (selectedRect[SR_TOPLEFT].contains(cursorPos))
    {
        this->setCursor(Qt::SizeFDiagCursor);
        return SR_TOPLEFT;
    }
    if (selectedRect[SR_TOP].contains(cursorPos))
    {
        this->setCursor(Qt::SizeVerCursor);
        return SR_TOP;
    }
    if (selectedRect[SR_TOPRIGHT].contains(cursorPos))
    {
        this->setCursor(Qt::SizeBDiagCursor);
        return SR_TOPRIGHT;
    }
    if (selectedRect[SR_RIGHT].contains(cursorPos))
    {
        this->setCursor(Qt::SizeHorCursor);
        return SR_RIGHT;
    }
    if (selectedRect[SR_BOTTOMRIGHT].contains(cursorPos))
    {
        this->setCursor(Qt::SizeFDiagCursor);
        return SR_BOTTOMRIGHT;
    }
    if (selectedRect[SR_BOTTOM].contains(cursorPos))
    {
        this->setCursor(Qt::SizeVerCursor);
        return SR_BOTTOM;
    }
    if (selectedRect[SR_BOTTOMLEFT].contains(cursorPos))
    {
        this->setCursor(Qt::SizeBDiagCursor);
        return SR_BOTTOMLEFT;
    }
    if (selectedRect[SR_LEFT].contains(cursorPos))
    {
        this->setCursor(Qt::SizeHorCursor);
        return SR_LEFT;
    }
    return SR_NULL;
}

void SelectRect::selectedRectChangeEvent(int SR_LOCATION, const QPoint& cursorPos)
{
    int x = cursorPos.x();
    int y = cursorPos.y();
    switch (SR_LOCATION)
    {
    case SR_NULL:
        // 限定在mask内
        selectedRect[SR_CENTER].setTopLeft(mouseLeftClickedPos);
        if (x < 0)
            x = 0;
        else if (x > this->width())
            x = this->width();
        if (y < 0)
            y = 0;
        else if (y > this->height())
            y = this->height();
        selectedRect[SR_CENTER].setWidth(x - selectedRect[SR_CENTER].x());
        selectedRect[SR_CENTER].setHeight(y - selectedRect[SR_CENTER].y());
        fixRectInfo(selectedRect[SR_CENTER]);
        break;
    case SR_CENTER:
        selectedRect[SR_CENTER].moveTo(lastSelectedRect.topLeft() + (cursorPos - mouseLeftClickedPos));
        break;
    case SR_TOPLEFT:
        selectedRect[SR_CENTER].setTopLeft(lastSelectedRect.topLeft() + (cursorPos - mouseLeftClickedPos));
        break;
    case SR_TOP:
        selectedRect[SR_CENTER].setTop(lastSelectedRect.top() + (cursorPos.y() - mouseLeftClickedPos.y()));
        break;
    case SR_TOPRIGHT:
        selectedRect[SR_CENTER].setTopRight(lastSelectedRect.topRight() + (cursorPos - mouseLeftClickedPos));
        break;
    case SR_RIGHT:
        selectedRect[SR_CENTER].setRight(lastSelectedRect.right() + (cursorPos.x() - mouseLeftClickedPos.x()));
        break;
    case SR_BOTTOMRIGHT:
        selectedRect[SR_CENTER].setBottomRight(lastSelectedRect.bottomRight() + (cursorPos - mouseLeftClickedPos));
        break;
    case SR_BOTTOM:
        selectedRect[SR_CENTER].setBottom(lastSelectedRect.bottom() + (cursorPos.y() - mouseLeftClickedPos.y()));
        break;
    case SR_BOTTOMLEFT:
        selectedRect[SR_CENTER].setBottomLeft(lastSelectedRect.bottomLeft() + (cursorPos - mouseLeftClickedPos));
        break;
    case SR_LEFT:
        selectedRect[SR_CENTER].setLeft(lastSelectedRect.left() + (cursorPos.x() - mouseLeftClickedPos.x()));
        break;
    default:
        break;
    }
}

void SelectRect::selectExit()
{
    emit sendSelectModeExit();
    this->close();
}

void SelectRect::selectReset()
{
    selectedRect[SR_CENTER] = QRect(0, 0, 0, 0);
    lastSelectedRect = QRect(0, 0, 0, 0);

    isSelectedRectStable = false;
    isSelectedRectExisted = false;
    // 关闭鼠标追踪 节省资源
    this->setMouseTracking(false);
    this->setCursor(Qt::ArrowCursor);
    cursorPosInSelectedArea = SR_NULL;
    mouseStatus = Qt::NoButton;
    update();
}

void SelectRect::cropZoomedImage()
{
    fixedRectInImage = getRectInImage(zoomedImage, drawImageTopLeftPos, selectedRect[SR_CENTER]);
    saveImage(zoomedImage, fixedRectInImage);
}

void SelectRect::cropOriginalImage()
{
    fixedRectInImage = getRectInImage(zoomedImage, drawImageTopLeftPos, selectedRect[SR_CENTER]);
    int x2 = fixedRectInImage.bottomRight().x();
    int y2 = fixedRectInImage.bottomRight().y();
    fixedRectInImage.setX(int(round(double(fixedRectInImage.x()) / double(zoomedImage->width()) * double(image->width()))));
    fixedRectInImage.setY(int(round(double(fixedRectInImage.y()) / double(zoomedImage->height()) * double(image->height()))));
    fixedRectInImage.setWidth(int(round(double(x2) / double(zoomedImage->width()) * double(image->width()))) - fixedRectInImage.x());
    fixedRectInImage.setHeight(int(round(double(y2) / double(zoomedImage->height()) * double(image->height()))) - fixedRectInImage.y());
    saveImage(image, fixedRectInImage);
}

void SelectRect::saveImage(const QImage* img, QRect rect)
{
    // 接受到截取框信息
    if (isLoadImage)
    {
        if (rect.width() > 0 && rect.height() > 0)
        {
            QImage saveImageTemp = img->copy(rect);
            QString filename = QFileDialog::getSaveFileName(this, tr("Save File"),
                QCoreApplication::applicationDirPath(),
                tr("Images (*.png *.xpm *.jpg *.tiff *.bmp)"));
            if (!filename.isEmpty() || !filename.isNull())
                saveImageTemp.save(filename);
        }
        else
        {
            QMessageBox msgBox(QMessageBox::Critical, tr("错误!"), tr("未选中图像!"));// Error! No Image Selected!
            msgBox.exec();
        }
    }
    else
    {
        QMessageBox msgBox(QMessageBox::Critical, tr("错误!"), tr("未选中图像!"));// Error! No Image Selected!
        msgBox.exec();
    }
}

void SelectRect::fixRectInfo(QRect& rect)
{
    QPoint topLeft = rect.topLeft();
    int width = rect.width();
    int height = rect.height();
    if (width < 0)
    {
        topLeft.setX(topLeft.x() + width);
        width = -width;
    }
    if (height < 0)
    {
        topLeft.setY(topLeft.y() + height);
        height = -height;
    }
    rect.setTopLeft(topLeft);
    rect.setSize(QSize(width, height));
}

ImageWidget::ImageWidget(QWidget* parent) : QWidget(parent)
{
    isImageLoaded = false;
    isSelectMode = false;
    qImageZoomedImage = new QImage;
    initializeContextmenu();
}

ImageWidget::~ImageWidget()
{
    if (isImageCloned)
        delete qImageContainer;
    if (isImageLoaded)
        qImageContainer = nullptr;
    delete qImageZoomedImage;
    qImageZoomedImage = nullptr;
}

void ImageWidget::setImageWithData(QImage& img)
{
    if (!isImageCloned)
    {
        qImageContainer = new QImage;
        isImageCloned = true;
    }
    if (img.isNull())
    {
        isImageLoaded = false;
        return;
    }
    else
    {
        *qImageContainer = img.copy();
        isImageLoaded = true;
        *qImageZoomedImage = img.copy();
        updateZoomedImage();
        if (!isEnableRecordLastParameters)
            setDefaultParameters();
        update();
    }
}

void ImageWidget::setImageWithPointer(QImage* img)
{
    if (img->isNull())
    {
        isImageLoaded = false;
        return;
    }
    else
    {
        qImageContainer = img;
        isImageLoaded = true;
        *qImageZoomedImage = img->copy();
        updateZoomedImage();
        if (!isEnableRecordLastParameters)
            setDefaultParameters();
        update();
    }
}

void ImageWidget::setImageWithFilePath(QString& path)
{
    if (!isImageCloned)
    {
        qImageContainer = new QImage;
        isImageCloned = true;
    }
    if (path.isEmpty() || path.isNull())
    {
        isImageLoaded = false;
        return;
    }
    else
    {
        qImageContainer->load(path);
        if (qImageContainer->isNull())
        {
            isImageLoaded = false;
            return;
        }
        isImageLoaded = true;
        *qImageZoomedImage = qImageContainer->copy();
        updateZoomedImage();
        if (!isEnableRecordLastParameters)
            setDefaultParameters();
        update();
    }
}

void ImageWidget::clear()
{
    if (isImageLoaded)
    {
        isImageLoaded = false;
        if (isImageCloned)
        {
            delete qImageContainer;
            qImageContainer = nullptr;
            isImageCloned = false;
        }
        else
            qImageContainer = nullptr;

        lastZoomedImageSize = QSize(0, 0);
        imageTopLeftRelativePosInWdigetX = 0.0;
        imageTopLeftRelativePosInWdigetY = 0.0;
        zoomScale = 1.0;
        mouseLeftClickedPos = QPoint(0, 0);
        drawImageTopLeftLastPos = QPoint(0, 0);
        drawImageTopLeftPos = QPoint(0, 0);
        update();
    }
}

void ImageWidget::setEnableOnlyShowImage(bool flag)
{
    isEnableOnlyShowImage = flag;
}

void ImageWidget::setEnableDragImage(bool flag)
{
    isEnableDragImage = flag;
    mActionEnableDrag->setChecked(isEnableDragImage);
}

void ImageWidget::setEnableZoomImage(bool flag)
{
    isEnableZoomImage = flag;
    mActionEnableZoom->setChecked(isEnableZoomImage);
}

void ImageWidget::setEnableImageFitWidget(bool flag)
{
    isEnableFitWidget = flag;
    mActionImageFitWidget->setChecked(isEnableFitWidget);
    resetImageWidget();
}

void ImageWidget::setEnableRecordLastParameters(bool flag)
{
    isEnableRecordLastParameters = flag;
    mActionRecordLastParameters->setChecked(isEnableRecordLastParameters);
}

void ImageWidget::setEnableSendLeftClickedPosInWidget(bool flag)
{
    isEnableSendLeftClickedPos = flag;
}

void ImageWidget::setEnableSendLeftClickedPosInImage(bool flag)
{
    isEnableSendLeftClickedPosInImage = flag;
}

QPoint ImageWidget::getDrawImageTopLeftPos() const
{
    return drawImageTopLeftPos;
}

void ImageWidget::wheelEvent(QWheelEvent* e)
{
    if (isImageLoaded && !isEnableOnlyShowImage && isEnableZoomImage)
    {
        int numDegrees = e->delta();
        if (numDegrees > 0)
        {
            imageZoomOut();
        }
        if (numDegrees < 0)
        {
            imageZoomIn();
        }
        updateZoomedImage();
        update();
    }
}

void ImageWidget::mousePressEvent(QMouseEvent* e)
{
    if (isImageLoaded && !isEnableOnlyShowImage)
    {
        switch (e->button())
        {
        case Qt::LeftButton:
            mouseStatus = Qt::LeftButton;
            mouseLeftClickedPos = e->pos();
            break;
        case Qt::RightButton:
            mouseStatus = Qt::RightButton;
            break;
        case Qt::MiddleButton:
            mouseStatus = Qt::MiddleButton;
            break;
        default:
            mouseStatus = Qt::NoButton;
        }
    }
}

void ImageWidget::mouseReleaseEvent(QMouseEvent* e)
{
    if (mouseStatus == Qt::LeftButton && !isImageDragging)
    {
        emitLeftClickedSignals(e);
        mouseStatus = Qt::NoButton;
    }
    if (isImageLoaded && !isEnableOnlyShowImage && isEnableDragImage)
    {
        if (mouseStatus == Qt::LeftButton)
        {
            // 记录上次图像顶点
            drawImageTopLeftLastPos = drawImageTopLeftPos;
            getImageLeftTopRelativePosInWidget(drawImageTopLeftPos.x(), drawImageTopLeftPos.y(),
                imageTopLeftRelativePosInWdigetX,
                imageTopLeftRelativePosInWdigetY);
            // 释放后鼠标状态置No
            mouseStatus = Qt::NoButton;
            isImageDragged = true;
            isImageDragging = false;
        }
    }
}

void ImageWidget::mouseMoveEvent(QMouseEvent* e)
{
    if (isImageLoaded && !isEnableOnlyShowImage && isEnableDragImage)
    {
        if (mouseStatus == Qt::LeftButton)
        {
            // e->pos()为当前鼠标坐标 转换为相对移动距离
            drawImageTopLeftPos = drawImageTopLeftLastPos + (e->pos() - mouseLeftClickedPos);
            isImageDragging = true;
            update();
        }
    }
}

void ImageWidget::paintEvent(QPaintEvent* e)
{
    QPainter painter(this);
    painter.setBrush(QBrush(QColor(200, 200, 200)));
    painter.drawRect(0, 0, this->width(), this->height());
    if (isImageLoaded)
        painter.drawImage(drawImageTopLeftPos, *qImageZoomedImage);
}

void ImageWidget::contextMenuEvent(QContextMenuEvent* e)
{
    if (!isEnableOnlyShowImage)
    {
        mMenu->exec(QCursor::pos());
        // 右键菜单弹出后 鼠标状态置No
        mouseStatus = Qt::NoButton;
    }
}

void ImageWidget::resizeEvent(QResizeEvent* e)
{
    if (isImageLoaded)
    {
        if (isEnableFitWidget)
            * qImageZoomedImage = qImageContainer->scaled(this->width() * zoomScale, this->height() * zoomScale, Qt::KeepAspectRatio);
        // 如果图像没有被拖拽或者缩放过 置中缩放
        if (!isImageDragged)
        {
            drawImageTopLeftLastPos = drawImageTopLeftPos = getPutImageInCenterPos(qImageZoomedImage, this);
        }
        else
        {
            drawImageTopLeftPos.setX(int(double(this->width()) * imageTopLeftRelativePosInWdigetX));
            drawImageTopLeftPos.setY(int(double(this->height()) * imageTopLeftRelativePosInWdigetY));
            drawImageTopLeftLastPos = drawImageTopLeftPos;
        }
        if (isSelectMode)
            emit sendParentWidgetSizeChangedSignal();
    }
}

void ImageWidget::resetImageWidget()
{
    setDefaultParameters();
    updateZoomedImage();
    isImageDragged = false;
    update();
}

void ImageWidget::imageZoomOut()
{
    if (zoomScale <= 8)
    {
        zoomScale *= 1.1;
        isZoomedParametersChanged = true;
    }
}

void ImageWidget::imageZoomIn()
{
    if (zoomScale >= 0.05)
    {
        zoomScale *= 1.0 / 1.1;
        isZoomedParametersChanged = true;
    }
}

void ImageWidget::select()
{
    if (isImageLoaded)
    {
        isSelectMode = true;
        SelectRect* m = new SelectRect(this);
        m->setGeometry(0, 0, this->geometry().width(), this->geometry().height());
        connect(m, SIGNAL(sendSelectModeExit()), this, SLOT(selectModeExit()));
        connect(this, SIGNAL(sendParentWidgetSizeChangedSignal()), m, SLOT(receiveParentSizeChangedSignal()));
        m->setImage(qImageContainer, qImageZoomedImage, drawImageTopLeftPos);
        m->show();
    }
}

void ImageWidget::initializeContextmenu()
{
    mMenu = new QMenu(this);
    mMenuAdditionalFunction = new QMenu(mMenu);

    mActionResetParameters = mMenu->addAction(tr("重置"));// Reset
    mActionSave = mMenu->addAction(tr("另存为")); // Save As
    mActionSelect = mMenu->addAction(tr("截取")); // Crop
    mMenuAdditionalFunction = mMenu->addMenu(tr("更多功能")); // More Function
    mActionEnableDrag = mMenuAdditionalFunction->addAction(tr("启用拖拽")); // Enable Drag
    mActionEnableZoom = mMenuAdditionalFunction->addAction(tr("启用缩放"));// Enable Zoom
    mActionImageFitWidget = mMenuAdditionalFunction->addAction(tr("自适应大小"));// Enable Image Fit Widget
    mActionRecordLastParameters = mMenuAdditionalFunction->addAction(tr("记住上次参数"));// Record Last Parameters (such as Scale, Image Position)

    mActionEnableDrag->setCheckable(true);
    mActionEnableZoom->setCheckable(true);
    mActionImageFitWidget->setCheckable(true);
    mActionRecordLastParameters->setCheckable(true);

    mActionEnableDrag->setChecked(isEnableDragImage);
    mActionEnableZoom->setChecked(isEnableZoomImage);
    mActionImageFitWidget->setChecked(isEnableFitWidget);
    mActionRecordLastParameters->setChecked(isEnableRecordLastParameters);

    connect(mActionResetParameters, SIGNAL(triggered()), this, SLOT(resetImageWidget()));
    connect(mActionSave, SIGNAL(triggered()), this, SLOT(save()));
    connect(mActionSelect, SIGNAL(triggered()), this, SLOT(select()));
    connect(mActionEnableDrag, SIGNAL(toggled(bool)), this, SLOT(setEnableDragImage(bool)));
    connect(mActionEnableZoom, SIGNAL(toggled(bool)), this, SLOT(setEnableZoomImage(bool)));
    connect(mActionImageFitWidget, SIGNAL(toggled(bool)), this, SLOT(setEnableImageFitWidget(bool)));
    connect(mActionRecordLastParameters, SIGNAL(toggled(bool)), this, SLOT(setEnableRecordLastParameters(bool)));
}

void ImageWidget::emitLeftClickedSignals(QMouseEvent* e)
{
    if (isEnableSendLeftClickedPos)
        emit sendLeftClickedPosInWidget(e->x(), e->y());

    if (isEnableSendLeftClickedPosInImage)
    {
        if (!isImageLoaded)
            emit sendLeftClickedPosInImage(-1, -1);
        else
        {
            QPoint cursorPosInImage = getCursorPosInImage(qImageContainer, qImageZoomedImage, drawImageTopLeftPos, e->pos());
            // 如果光标不在图像上则返回-1
            if (cursorPosInImage.x() < 0 || cursorPosInImage.y() < 0 ||
                cursorPosInImage.x() > qImageContainer->width() - 1 ||
                cursorPosInImage.y() > qImageContainer->height() - 1)
            {
                cursorPosInImage.setX(-1);
                cursorPosInImage.setY(-1);
            }
            emit sendLeftClickedPosInImage(cursorPosInImage.x(), cursorPosInImage.y());
        }
    }
}

QPoint ImageWidget::getCursorPosInImage(const QImage* originalImage, const QImage* zoomedImage, const QPoint& imageLeftTopPos, QPoint cursorPos)
{
    // 计算当前光标在原始图像坐标系中相对于图像原点的位置
    QPoint returnPoint;
    int distanceX = cursorPos.x() - imageLeftTopPos.x();
    int distanceY = cursorPos.y() - imageLeftTopPos.y();
    double xDivZoomedImageW = double(distanceX) / double(zoomedImage->width());
    double yDivZoomedImageH = double(distanceY) / double(zoomedImage->height());
    returnPoint.setX(int(double(originalImage->width()) * xDivZoomedImageW));
    returnPoint.setY(int(double(originalImage->height()) * yDivZoomedImageH));
    return returnPoint;
}

QPoint ImageWidget::getCursorPosInZoomedImage(QPoint cursorPos)
{
    // 计算当前光标在缩放后图像坐标系中相对于图像原点的位置
    return cursorPos - drawImageTopLeftLastPos;
}

void ImageWidget::getImageLeftTopRelativePosInWidget(const int x, const int y, double& returnX, double& returnY)
{
    returnX = double(x) / double(this->width());
    returnY = double(y) / double(this->height());
}

void ImageWidget::save()
{
    if (isImageLoaded)
    {
        QImage temp = qImageContainer->copy();
        QString filename = QFileDialog::getSaveFileName(this, tr("Open File"),
            QCoreApplication::applicationDirPath(),
            tr("Images (*.png *.xpm *.jpg *.tiff *.bmp)"));
        if (!filename.isEmpty() || !filename.isNull())
            temp.save(filename);
    }
}

void ImageWidget::selectModeExit()
{
    isSelectMode = false;
}

void ImageWidget::updateZoomedImage()
{
    QPoint cursorPosInWidget, cursorPosInImage;
    // 图像为空直接返回
    if (!isImageLoaded)
        return;

    if (isZoomedParametersChanged)
    {
        lastZoomedImageSize = qImageZoomedImage->size();
        // 获取当前光标并计算出光标在图像中的位置
        cursorPosInWidget = this->mapFromGlobal(QCursor::pos());
        cursorPosInImage = getCursorPosInImage(qImageContainer, qImageZoomedImage, drawImageTopLeftPos, cursorPosInWidget);
    }

    // 减少拖动带来的QImage::scaled
    if (isEnableFitWidget)
        * qImageZoomedImage = qImageContainer->scaled(this->width() * zoomScale, this->height() * zoomScale, Qt::KeepAspectRatio);
    else
        *qImageZoomedImage = qImageContainer->scaled(qImageContainer->width() * zoomScale, qImageContainer->height() * zoomScale, Qt::KeepAspectRatio);

    if (isZoomedParametersChanged)
    {
        QSize zoomedImageChanged = lastZoomedImageSize - qImageZoomedImage->size();

        // 根据光标在图像的位置进行调整左上绘图点位置
        drawImageTopLeftPos += QPoint(int(double(zoomedImageChanged.width()) * double(cursorPosInImage.x()) / double(qImageContainer->width() - 1)),
            int(double(zoomedImageChanged.height()) * double(cursorPosInImage.y()) / double(qImageContainer->height() - 1)));

        drawImageTopLeftLastPos = drawImageTopLeftPos;

        getImageLeftTopRelativePosInWidget(drawImageTopLeftPos.x(), drawImageTopLeftPos.y(),
            imageTopLeftRelativePosInWdigetX,
            imageTopLeftRelativePosInWdigetY);
        isZoomedParametersChanged = false;
    }
}

void ImageWidget::setDefaultParameters()
{
    zoomScale = 1.0;
    if (isImageLoaded)
    {
        // 首先恢复zoomedImage大小再调整drawPos
        updateZoomedImage();
        drawImageTopLeftPos = drawImageTopLeftLastPos = getPutImageInCenterPos(qImageZoomedImage, this);
        getImageLeftTopRelativePosInWidget(drawImageTopLeftPos.x(), drawImageTopLeftPos.y(),
            imageTopLeftRelativePosInWdigetX,
            imageTopLeftRelativePosInWdigetY);
    }
}

QPoint ImageWidget::getPutImageInCenterPos(const QImage* showImage, const QWidget* ImageWidget)
{
    QPoint returnPoint;
    returnPoint.setX(int(double(ImageWidget->width() - showImage->width()) / 2));
    returnPoint.setY(int(double(ImageWidget->height() - showImage->height()) / 2));
    return returnPoint;
}
