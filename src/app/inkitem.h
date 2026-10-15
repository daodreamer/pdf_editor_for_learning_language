#pragma once
#include "annotationmodel.h"
#include <QPointer>
#include <QQuickItem>

class InkItem : public QQuickItem {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(AnnotationModel* model READ model WRITE setModel NOTIFY modelChanged)
    Q_PROPERTY(int page READ page WRITE setPage NOTIFY pageChanged)
public:
    explicit InkItem(QQuickItem* parent=nullptr);
    AnnotationModel* model() const { return model_; }
    void setModel(AnnotationModel* model);
    int page() const { return page_; }
    void setPage(int page);
signals:
    void modelChanged();
    void pageChanged();
protected:
    QSGNode* updatePaintNode(QSGNode*,UpdatePaintNodeData*) override;
private:
    QPointer<AnnotationModel> model_;
    int page_=-1;
};
