/* This file is part of the KDE libraries
 *
 * Copyright (c) 2011 Aurélien Gâteau <agateau@kde.org>
 * Copyright (c) 2014 Dominik Haumann <dhaumann@kde.org>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301  USA
 */
#include "MessageWidget.hpp"
#include <QAction>
#include <QEvent>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QToolButton>
#include <QStyle>
#include <QGuiApplication>
//---------------------------------------------------------------------
// MessageWidgetPrivate
//---------------------------------------------------------------------
class MessageWidgetPrivate
{
public:
    void init(MessageWidget *);
    MessageWidget *q;
    QFrame *content = nullptr;
    QLabel *iconLabel = nullptr;
    QLabel *textLabel = nullptr;
    QIcon icon;
    bool ignoreShowEventDoingAnimatedShow = false;
    MessageWidget::MessageType messageType;
    bool wordWrap;
    QList<QToolButton *> buttons;
    void createLayout();
    void applyStyleSheet();
    void updateLayout();
    int bestContentHeight() const;
};
void MessageWidgetPrivate::init(MessageWidget *q_ptr)
{
    q = q_ptr;
    q->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Fixed);
    content = new QFrame(q);
    content->setObjectName(QStringLiteral("contentWidget"));
    content->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    wordWrap = false;
    iconLabel = new QLabel(content);
    iconLabel->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    iconLabel->hide();
    textLabel = new QLabel(content);
    textLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    textLabel->setTextInteractionFlags(Qt::TextBrowserInteraction);
    textLabel->setOpenExternalLinks(true);
    QObject::connect(textLabel, &QLabel::linkActivated, q, &MessageWidget::linkActivated);
    QObject::connect(textLabel, &QLabel::linkHovered, q, &MessageWidget::linkHovered);
    q->setMessageType(MessageWidget::Information);
}
void MessageWidgetPrivate::createLayout()
{
    delete content->layout();
    content->resize(q->size());
    qDeleteAll(buttons);
    buttons.clear();

    Q_FOREACH(QAction *action, q->actions())
    {
        QToolButton *button = new QToolButton(content);
        button->setDefaultAction(action);
        button->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        buttons.append(button);
    }

    if(wordWrap)
    {
        QGridLayout *layout = new QGridLayout(content);
        // Set alignment to make sure icon does not move down if text wraps
        layout->addWidget(iconLabel, 0, 0, 1, 1, Qt::AlignHCenter | Qt::AlignTop);
        layout->addWidget(textLabel, 0, 1);

        if(!buttons.isEmpty())
        {
            // Use an additional layout in row 1 for the buttons.
            QHBoxLayout *buttonLayout = new QHBoxLayout(content);
            buttonLayout->addStretch();

            Q_FOREACH(QToolButton *button, buttons)
            {
                // For some reason, calling show() is necessary if wordwrap is true,
                // otherwise the buttons do not show up. It is not needed if
                // wordwrap is false.
                button->show();
                buttonLayout->addWidget(button);
            }
        }
    }
    else
    {
        QHBoxLayout *layout = new QHBoxLayout(content);
        layout->addWidget(iconLabel);
        layout->addWidget(textLabel);

        for(QToolButton *button : qAsConst(buttons))
        {
            layout->addWidget(button);
        }
    };

    if(q->isVisible())
    {
        q->setFixedHeight(content->sizeHint().height());
    }

    q->updateGeometry();
}
void MessageWidgetPrivate::applyStyleSheet()
{
    QColor bgBaseColor;

    // We have to hardcode colors here because KWidgetsAddons is a tier 1 framework
    // and therefore can't depend on any other KDE Frameworks
    // The following RGB color values come from the "default" scheme in kcolorscheme.cpp
    switch(messageType)
    {
    case MessageWidget::Positive:
        bgBaseColor.setRgb(39, 174,  96); // Window: ForegroundPositive
        break;

    case MessageWidget::Information:
        bgBaseColor.setRgb(61, 174, 233); // Window: ForegroundActive
        break;

    case MessageWidget::Warning:
        bgBaseColor.setRgb(246, 116, 0); // Window: ForegroundNeutral
        break;

    case MessageWidget::Error:
        bgBaseColor.setRgb(218, 68, 83); // Window: ForegroundNegative
        break;
    }

    const qreal bgBaseColorAlpha = 0.2;
    bgBaseColor.setAlphaF(bgBaseColorAlpha);
    const QPalette palette = QGuiApplication::palette();
    const QColor windowColor = palette.window().color();
    const QColor textColor = palette.text().color();
    const QColor border = bgBaseColor;
    // Generate a final background color from overlaying bgBaseColor over windowColor
    const int newRed = (bgBaseColor.red() * bgBaseColorAlpha) + (windowColor.red() * (1 - bgBaseColorAlpha));
    const int newGreen = (bgBaseColor.green() * bgBaseColorAlpha) + (windowColor.green() * (1 - bgBaseColorAlpha));
    const int newBlue = (bgBaseColor.blue() * bgBaseColorAlpha) + (windowColor.blue() * (1 - bgBaseColorAlpha));
    const QColor bgFinalColor = QColor(newRed, newGreen, newBlue);
    content->setStyleSheet(
        QString::fromLatin1(".QFrame {"
                            "background-color: %1;"
                            "border-radius: 4px;"
                            "border: 2px solid %2;"
                            "margin: %3px;"
                            "}"
                            ".QLabel { color: %4; }"
                           )
        .arg(bgFinalColor.name())
        .arg(border.name())
        // DefaultFrameWidth returns the size of the external margin + border width. We know our border is 1px, so we subtract this from the frame normal QStyle FrameWidth to get our margin
        .arg(q->style()->pixelMetric(QStyle::PM_DefaultFrameWidth, nullptr, q) - 1)
        .arg(textColor.name())
    );
}
void MessageWidgetPrivate::updateLayout()
{
    if(content->layout())
    {
        createLayout();
    }
}
int MessageWidgetPrivate::bestContentHeight() const
{
    int height = content->heightForWidth(q->width());

    if(height == -1)
    {
        height = content->sizeHint().height();
    }

    return height;
}
//---------------------------------------------------------------------
// MessageWidget
//---------------------------------------------------------------------
MessageWidget::MessageWidget(QWidget *parent)
    : QFrame(parent)
    , d(new MessageWidgetPrivate)
{
    d->init(this);
}
MessageWidget::MessageWidget(const QString &text, QWidget *parent)
    : QFrame(parent)
    , d(new MessageWidgetPrivate)
{
    d->init(this);
    setText(text);
}
MessageWidget::~MessageWidget()
{
    delete d;
}
QString MessageWidget::text() const
{
    return d->textLabel->text();
}
void MessageWidget::setText(const QString &text)
{
    d->textLabel->setText(text);
    updateGeometry();
}
MessageWidget::MessageType MessageWidget::messageType() const
{
    return d->messageType;
}
void MessageWidget::setMessageType(MessageWidget::MessageType type)
{
    d->messageType = type;
    d->applyStyleSheet();
}
QSize MessageWidget::sizeHint() const
{
    ensurePolished();
    return d->content->sizeHint();
}
QSize MessageWidget::minimumSizeHint() const
{
    ensurePolished();
    return d->content->minimumSizeHint();
}
bool MessageWidget::event(QEvent *event)
{
    if(event->type() == QEvent::Polish && !d->content->layout())
    {
        d->createLayout();
    }
    else if(event->type() == QEvent::PaletteChange)
    {
        d->applyStyleSheet();
    }
    else if(event->type() == QEvent::Show && !d->ignoreShowEventDoingAnimatedShow)
    {
        if((height() != d->content->height()) || (d->content->pos().y() != 0))
        {
            d->content->move(0, 0);
            setFixedHeight(d->content->height());
        }
    }

    return QFrame::event(event);
}
void MessageWidget::resizeEvent(QResizeEvent *event)
{
    QFrame::resizeEvent(event);
    d->content->resize(width(), d->bestContentHeight());
}
int MessageWidget::heightForWidth(int width) const
{
    ensurePolished();
    return d->content->heightForWidth(width);
}
bool MessageWidget::wordWrap() const
{
    return d->wordWrap;
}
void MessageWidget::setWordWrap(bool wordWrap)
{
    d->wordWrap = wordWrap;
    d->textLabel->setWordWrap(wordWrap);
    QSizePolicy policy = sizePolicy();
    policy.setHeightForWidth(wordWrap);
    setSizePolicy(policy);
    d->updateLayout();

    // Without this, when user does wordWrap -> !wordWrap -> wordWrap, a minimum
    // height is set, causing the widget to be too high.
    // Mostly visible in test programs.
    if(wordWrap)
    {
        setMinimumHeight(0);
    }
}
bool MessageWidget::isCloseButtonVisible() const
{
    return false;
}
void MessageWidget::setCloseButtonVisible(bool)
{
    updateGeometry();
}
void MessageWidget::addAction(QAction *action)
{
    QFrame::addAction(action);
    d->updateLayout();
}
void MessageWidget::removeAction(QAction *action)
{
    QFrame::removeAction(action);
    d->updateLayout();
}
QIcon MessageWidget::icon() const
{
    return d->icon;
}
void MessageWidget::setIcon(const QIcon &icon)
{
    d->icon = icon;

    if(d->icon.isNull())
    {
        d->iconLabel->hide();
    }
    else
    {
        const int size = style()->pixelMetric(QStyle::PM_ToolBarIconSize);
        d->iconLabel->setPixmap(d->icon.pixmap(size));
        d->iconLabel->show();
    }
}

