#ifndef CURSORUPDATEHELPER_H
#define CURSORUPDATEHELPER_H

#include <QWidget>

namespace GuiLocations {

class CursorUpdateHelper
{
public:
    CursorUpdateHelper(QWidget *widget);

    void setForbiddenCursor();
    void setPointingHandCursor();

private:
    void applyCurrentCursor();

    QWidget *widget_;
    Qt::CursorShape currentCursor_;
};

} // namespace GuiLocations

#endif // CURSORUPDATEHELPER_H
