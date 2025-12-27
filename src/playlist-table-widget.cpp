#include "playlist-table-widget.hpp"
#include "mpv-dock.hpp"
#include "obs-mpv-source.hpp"

#include <QtGui/QDragEnterEvent>
#include <QtGui/QDropEvent>
#include <QtCore/QMimeData>
#include <QtWidgets/QApplication>
#include <QtGui/QDrag>

PlaylistTableWidget::PlaylistTableWidget(MpvControlDock *dock, QWidget *parent)
    : QTableWidget(parent), m_dock(dock)
{
    setAcceptDrops(true);
    setDragDropMode(QAbstractItemView::InternalMove);
    setDragDropOverwriteMode(false);
}

void PlaylistTableWidget::dragEnterEvent(QDragEnterEvent *event) {
    if (event->mimeData()->hasUrls() || event->source() == this) {
        event->acceptProposedAction();
    }
}

void PlaylistTableWidget::dragMoveEvent(QDragMoveEvent *event) {
    if (event->mimeData()->hasUrls() || event->source() == this) {
        event->acceptProposedAction();
    }
}

void PlaylistTableWidget::dropEvent(QDropEvent *event) {
    ObsMpvSource *source = m_dock->getCurrentMpvSource();
    if (!source) return;

    if (event->mimeData()->hasUrls()) {
        std::vector<std::string> paths;
        for (const QUrl &url : event->mimeData()->urls()) {
            if (url.isLocalFile()) {
                paths.push_back(url.toLocalFile().toStdString());
            }
        }
        if (!paths.empty()) {
            source->playlist_add_multiple(paths);
            m_dock->updatePlaylistTable();
        }
        event->acceptProposedAction();
    } else if (event->source() == this) {
        int fromRow = selectionModel()->currentIndex().row();
        int toRow = indexAt(event->position().toPoint()).row();
        if (toRow == -1) toRow = rowCount() - 1;
        
        performInternalMove(fromRow, toRow);
        event->accept();
    }
}

void PlaylistTableWidget::startDrag(Qt::DropActions /*supportedActions*/) {
    QDrag *drag = new QDrag(this);
    QMimeData *mimeData = new QMimeData;
    mimeData->setText("internal-move"); // Dummy data
    drag->setMimeData(mimeData);
    drag->exec(Qt::MoveAction);
}

void PlaylistTableWidget::performInternalMove(int from, int to) {
    ObsMpvSource *source = m_dock->getCurrentMpvSource();
    if (!source) return;
    
    if (from != to) {
        source->playlist_move(from, to);
        m_dock->updatePlaylistTable();
        setCurrentCell(to, 0);
    }
}

void PlaylistTableWidget::mouseDoubleClickEvent(QMouseEvent *event)
{
    QTableWidgetItem *item = itemAt(event->pos());
    if (item) {
        ObsMpvSource *source = m_dock->getCurrentMpvSource();
        if (source) {
            source->playlist_play(item->row());
        }
    }
    QTableWidget::mouseDoubleClickEvent(event);
}
