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
     
      void setPath(const QString &newpath);
      QString getPath() const;

      bool isHistoryAtFirstIndex() const;
      bool isHistoryAtLastIndex() const;
     
      void hidePopup() override;
      void showPopup() override;
      bool eventFilter(QObject* object, QEvent* event) override;
      bool event(QEvent *e) override; 

   public slots:
      void goHistoryBack();
      void goHistoryForward();
      void goToHomeDirectory();

   signals:
      void pathChanged(const QString &path);

   protected:
      void navigateTo(const QString &path, bool fromhistory = false);

   protected slots:
      void onIndexChanged(int index);

   private:
    struct Impl;
    std::unique_ptr<Impl> d;
};

