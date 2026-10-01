#pragma once
/*
 * SizeCard.h — clickable photo-size tile for the selection grid.
 */

#include <QFrame>
#include <QLabel>
#include <QVBoxLayout>
#include <QMouseEvent>
#include "ImageService.h"   // PhotoSize

class SizeCard : public QFrame {
    Q_OBJECT
public:
    SizeCard(const PhotoSize& s, int index, QWidget* parent = nullptr)
        : QFrame(parent), index_(index) {
        setMinimumSize(250, 140);
        setCursor(Qt::PointingHandCursor);

        auto* l = new QVBoxLayout(this);
        l->setAlignment(Qt::AlignCenter);
        l->setSpacing(4);

        auto* name = new QLabel(s.name, this);
        name->setAlignment(Qt::AlignCenter);
        name->setStyleSheet("color: white; font-size: 22px; font-weight: 600;"
                            "background: transparent;");

        auto* mm = new QLabel(QString("%1 × %2 mm").arg(s.w_mm).arg(s.h_mm), this);
        mm->setAlignment(Qt::AlignCenter);
        mm->setStyleSheet("color: rgba(255,255,255,170); font-size: 13px;"
                          "background: transparent;");

        l->addWidget(name);
        l->addWidget(mm);
        setSelected(false);
    }

    void setSelected(bool sel) {
        selected_ = sel;
        setStyleSheet(sel
            ? "QFrame { background: #1E2A33; border-radius: 14px;"
              "         border: 3px solid #FFE84A; }"
            : "QFrame { background: #808080; border-radius: 14px;"
              "         border: 3px solid transparent; }");
    }

    bool selected() const { return selected_; }
    int  index()    const { return index_; }

signals:
    void clicked(int index);

protected:
    void mousePressEvent(QMouseEvent* e) override {
        if (e->button() == Qt::LeftButton) emit clicked(index_);
        QFrame::mousePressEvent(e);
    }

private:
    int  index_;
    bool selected_ = false;
};
