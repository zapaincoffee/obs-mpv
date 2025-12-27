#pragma once

#include <QtWidgets/QTableWidget>

class MpvControlDock;

class PlaylistTableWidget : public QTableWidget {
    Q_OBJECT

public:
    explicit PlaylistTableWidget(MpvControlDock *dock, QWidget *parent = nullptr);

protected:
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dragMoveEvent(QDragMoveEvent *event) override;
    void dropEvent(QDropEvent *event) override;
    void startDrag(Qt::DropActions supportedActions) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;

private:
    MpvControlDock *m_dock;
    
    void performInternalMove(int from, int to);
};
