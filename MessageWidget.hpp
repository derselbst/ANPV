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
#ifndef MessageWidget_H
#define MessageWidget_H
#include <QFrame>
#include <QString>
#include <QIcon>
#include <QSize>
class MessageWidgetPrivate;
class QPaintEvent;
class QEvent;
class QResizeEvent;
/**
 * @class MessageWidget MessageWidget.h MessageWidget
 *
 * @short A widget to provide feedback or propose opportunistic interactions.
 *
 * MessageWidget can be used to provide inline positive or negative
 * feedback, or to implement opportunistic interactions.
 *
 * As a feedback widget, MessageWidget provides a less intrusive alternative
 * to "OK Only" message boxes. If you want to avoid a modal KMessageBox,
 * consider using MessageWidget instead.
 *
 * Examples of MessageWidget look as follows, all of them having an icon set
 * with setIcon(), and the first three show a close button:
 *
 * \image html MessageWidget.png "MessageWidget with different message types"
 *
 * <b>Negative feedback</b>
 *
 * The MessageWidget can be used as a secondary indicator of failure: the
 * first indicator is usually the fact the action the user expected to happen
 * did not happen.
 *
 * Example: User fills a form, clicks "Submit".
 *
 * @li Expected feedback: form closes
 * @li First indicator of failure: form stays there
 * @li Second indicator of failure: a MessageWidget appears on top of the
 * form, explaining the error condition
 *
 * When used to provide negative feedback, MessageWidget should be placed
 * close to its context. In the case of a form, it should appear on top of the
 * form entries.
 *
 * MessageWidget should get inserted in the existing layout. Space should not
 * be reserved for it, otherwise it becomes "dead space", ignored by the user.
 * MessageWidget should also not appear as an overlay to prevent blocking
 * access to elements the user needs to interact with to fix the failure.
 *
 * <b>Positive feedback</b>
 *
 * MessageWidget can be used for positive feedback but it shouldn't be
 * overused. It is often enough to provide feedback by simply showing the
 * results of an action.
 *
 * Examples of acceptable uses:
 *
 * @li Confirm success of "critical" transactions
 * @li Indicate completion of background tasks
 *
 * Example of unadapted uses:
 *
 * @li Indicate successful saving of a file
 * @li Indicate a file has been successfully removed
 *
 * <b>Opportunistic interaction</b>
 *
 * Opportunistic interaction is the situation where the application suggests to
 * the user an action he could be interested in perform, either based on an
 * action the user just triggered or an event which the application noticed.
 *
 * Example of acceptable uses:
 *
 * @li A browser can propose remembering a recently entered password
 * @li A music collection can propose ripping a CD which just got inserted
 * @li A chat application may notify the user a "special friend" just connected
 *
 * @author Aurélien Gâteau <agateau@kde.org>
 * @since 4.7
 */
class MessageWidget : public QFrame
{
    Q_OBJECT
    Q_PROPERTY(QString text READ text WRITE setText)
    Q_PROPERTY(bool wordWrap READ wordWrap WRITE setWordWrap)
    Q_PROPERTY(bool closeButtonVisible READ isCloseButtonVisible WRITE setCloseButtonVisible)
    Q_PROPERTY(MessageType messageType READ messageType WRITE setMessageType)
    Q_PROPERTY(QIcon icon READ icon WRITE setIcon)
public:
    /**
     * Available message types.
     * The background colors are chosen depending on the message type.
     */
    enum MessageType {
        Positive,
        Information,
        Warning,
        Error
    };
    Q_ENUM(MessageType)
    /**
     * Constructs a MessageWidget with the specified @p parent.
     */
    explicit MessageWidget(QWidget *parent = nullptr);
    /**
     * Constructs a MessageWidget with the specified @p parent and
     * contents @p text.
     */
    explicit MessageWidget(const QString &text, QWidget *parent = nullptr);
    /**
     * Destructor.
     */
    ~MessageWidget() override;
    /**
     * Get the text of this message widget.
     * @see setText()
     */
    QString text() const;
    /**
     * Check whether word wrap is enabled.
     *
     * If word wrap is enabled, the message widget wraps the displayed text
     * as required to the available width of the widget. This is useful to
     * avoid breaking widget layouts.
     *
     * @see setWordWrap()
     */
    bool wordWrap() const;
    /**
     * Check whether the close button is visible.
     *
     * @see setCloseButtonVisible()
     */
    bool isCloseButtonVisible() const;
    /**
     * Get the type of this message.
     * By default, the type is set to MessageWidget::Information.
     *
     * @see MessageWidget::MessageType, setMessageType()
     */
    MessageType messageType() const;
    /**
     * Add @p action to the message widget.
     * For each action a button is added to the message widget in the
     * order the actions were added.
     *
     * @param action the action to add
     * @see removeAction(), QWidget::actions()
     */
    void addAction(QAction *action);
    /**
     * Remove @p action from the message widget.
     *
     * @param action the action to remove
     * @see MessageWidget::MessageType, addAction(), setMessageType()
     */
    void removeAction(QAction *action);
    /**
     * Returns the preferred size of the message widget.
     */
    QSize sizeHint() const override;
    /**
     * Returns the minimum size of the message widget.
     */
    QSize minimumSizeHint() const override;
    /**
     * Returns the required height for @p width.
     * @param width the width in pixels
     */
    int heightForWidth(int width) const override;
    /**
     * The icon shown on the left of the text. By default, no icon is shown.
     * @since 4.11
     */
    QIcon icon() const;
public Q_SLOTS:
    /**
     * Set the text of the message widget to @p text.
     * If the message widget is already visible, the text changes on the fly.
     *
     * @param text the text to display, rich text is allowed
     * @see text()
     */
    void setText(const QString &text);
    /**
     * Set word wrap to @p wordWrap. If word wrap is enabled, the text()
     * of the message widget is wrapped to fit the available width.
     * If word wrap is disabled, the message widget's minimum size is
     * such that the entire text fits.
     *
     * @param wordWrap disable/enable word wrap
     * @see wordWrap()
     */
    void setWordWrap(bool wordWrap);
    /**
     * Set the visibility of the close button. If @p visible is @e true,
     * a close button is shown that calls animatedHide() if clicked.
     *
     * @see closeButtonVisible(), animatedHide()
     */
    void setCloseButtonVisible(bool visible);
    /**
     * Set the message type to @p type.
     * By default, the message type is set to MessageWidget::Information.
     * Appropriate colors are chosen to mimic the appearance of Kirigami's
     * InlineMessage.
     *
     * @see messageType(), MessageWidget::MessageType
     */
    void setMessageType(MessageWidget::MessageType type);
    /**
     * Define an icon to be shown on the left of the text
     * @since 4.11
     */
    void setIcon(const QIcon &icon);
Q_SIGNALS:
    /**
     * This signal is emitted when the user clicks a link in the text label.
     * The URL referred to by the href anchor is passed in contents.
     * @param contents text of the href anchor
     * @see QLabel::linkActivated()
     * @since 4.10
     */
    void linkActivated(const QString &contents);
    /**
     * This signal is emitted when the user hovers over a link in the text label.
     * The URL referred to by the href anchor is passed in contents.
     * @param contents text of the href anchor
     * @see QLabel::linkHovered()
     * @since 4.11
     */
    void linkHovered(const QString &contents);
protected:
    bool event(QEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
private:
    MessageWidgetPrivate *const d;
};
#endif /* MessageWidget_H */
