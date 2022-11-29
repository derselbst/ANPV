/* Copyright (C) 2022 Martin Pietsch <@pmfoss>
   SPDX-License-Identifier: BSD-3-Clause */
/* Modified by derselbst for ANPV */

#pragma once

#include <QComboBox>
#include <memory>

/* class UrlNavigatorWidget */

class UrlNavigatorWidget : public QComboBox
{
   Q_OBJECT

   public:
      UrlNavigatorWidget(QWidget *parent = nullptr);
      UrlNavigatorWidget(const QString &syspath, QWidget *parent = nullptr);
      ~UrlNavigatorWidget() override;
     
      QString getPath() const;

      bool isHistoryAtFirstIndex() const;
      bool isHistoryAtLastIndex() const;
     
      void hidePopup() override;
      void showPopup() override;
      bool eventFilter(QObject* object, QEvent* event) override;
      void keyPressEvent(QKeyEvent *e) override;

   public slots:
      void setPath(const QString &path);

   signals:
      void pathChangedByUser(const QString &path);

   protected slots:
      void onIndexChanged(int index);
      void navigateTo(const QString &path);

   private:
    struct Impl;
    std::unique_ptr<Impl> d;
};

