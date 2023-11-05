#include <QProxyStyle>
#include <QStyle>

class QWidget;
class QStyleOption;

class CenteredBoxProxyStyle : public QProxyStyle
{
public:
    using QProxyStyle::QProxyStyle;
    QRect subElementRect(QStyle::SubElement element, const QStyleOption *option, const QWidget *widget) const override;
};