#pragma once
#include "annotationmodel.h"
#include <QPointer>
#include <QQuickItem>

class InkItem : public QQuickItem {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(AnnotationModel* model READ model WRITE setModel NOTIFY modelChanged)
    Q_PROPERTY(int page READ page WRITE setPage NOTIFY pageChanged)
    // Draws the page's comparison round (AnnotationModel::overlayStrokes) instead of the current ink.
    Q_PROPERTY(bool overlay READ overlay WRITE setOverlay NOTIFY overlayChanged)
public:
    explicit InkItem(QQuickItem* parent=nullptr);
    AnnotationModel* model() const { return model_; }
    void setModel(AnnotationModel* model);
    int page() const { return page_; }
    void setPage(int page);
    bool overlay() const { return overlay_; }
    void setOverlay(bool overlay) { if(overlay_!=overlay) { overlay_=overlay; update(); emit overlayChanged(); } }
signals:
    void overlayChanged();
    void modelChanged();
    void pageChanged();
protected:
    QSGNode* updatePaintNode(QSGNode*,UpdatePaintNodeData*) override;
private:
    QPointer<AnnotationModel> model_;
    int page_=-1;
    bool overlay_=false;
};
