/*
 *  Author:
 *     Guido Tack <guido.tack@monash.edu>
 *
 *  Copyright:
 *     NICTA 2013
 */

/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include <QtWidgets>
#include "codeeditor.h"
#include "ide.h"
#include "mainwindow.h"

void
CodeEditor::initUI(QFont& font)
{
    setFont(font);
    QFontMetrics metrics(font);
    setTabStopDistance(metrics.horizontalAdvance(' ') * indentSize);

    lineNumbers= new LineNumbers(this);
    debugInfo = new DebugInfo(this);
    editorHeader = new EditorHeader(this);
    debugInfo->hide();
    connect(this, &QPlainTextEdit::blockCountChanged, this, &CodeEditor::setViewportWidth);
    connect(this, &QPlainTextEdit::updateRequest, this, &CodeEditor::setLineNumbers);
    connect(this, &QPlainTextEdit::updateRequest, this, &CodeEditor::setDebugInfoPos);
    connect(this, &QPlainTextEdit::cursorPositionChanged, this, &CodeEditor::cursorChange);
    connect(document(), &QTextDocument::modificationChanged, this, &CodeEditor::docChanged);
    connect(document(), &QTextDocument::contentsChanged, this, &CodeEditor::contentsChanged);

    setViewportWidth(0);
    cursorChange();

    highlighter = new Highlighter(font,darkMode,document());
    setDarkMode(darkMode);

    QTextCursor cursor(textCursor());
    cursor.movePosition(QTextCursor::Start);
    setTextCursor(cursor);
    ensureCursorVisible();
    setFocus();
}

CodeEditor::CodeEditor(QTextDocument* doc, const QString& path, bool isNewFile, bool large,
                       QFont& font, int indentSize0, bool useTabs0, bool darkMode0,
                       QTabWidget* t, QWidget *parent) :
    QPlainTextEdit(parent), loadContentsButton(nullptr), tabs(t),
    indentSize(indentSize0), useTabs(useTabs0), darkMode(darkMode0)
{
    if (doc) {
        QPlainTextEdit::setDocument(doc);
    }
    initUI(font);
    if (isNewFile) {
        filepath = "";
        filename = path;
    } else {
        filepath = QFileInfo(path).absoluteFilePath();
        filename = QFileInfo(path).fileName();
    }
    if (large) {
        setReadOnly(true);
        QPushButton* pb = new QPushButton("Big file. Load contents?", this);
        connect(pb, &QPushButton::clicked, this, &CodeEditor::loadContents);
        loadContentsButton = pb;
    }
    completer = new QCompleter(this);
    QStringList completionList;
    completionList << "annotation" << "array" << "bool" << "constraint"
                   << "diff" << "else" << "elseif" << "endif" << "enum" << "float"
                   << "function" << "include" << "intersect" << "maximize" << "minimize"
                   << "output" << "predicate" << "satisfy" << "solve" << "string"
                   << "subset" << "superset" << "symdiff" << "test" << "then"
                   << "union" << "where";
    completionList.sort();
    completionModel.setStringList(completionList);
    completer->setModel(&completionModel);
    completer->setCaseSensitivity(Qt::CaseSensitive);
    completer->setModelSorting(QCompleter::CaseSensitivelySortedModel);
    completer->setWrapAround(false);
    completer->setWidget(this);
    completer->setCompletionMode(QCompleter::PopupCompletion);
    QObject::connect(completer, QOverload<const QString&>::of(&QCompleter::activated), this, &CodeEditor::insertCompletion);

    modificationTimer.setSingleShot(true);
    QObject::connect(&modificationTimer, &QTimer::timeout, this, &CodeEditor::contentsChangedWithTimeout);

    auto* mw = qobject_cast<MainWindow*>(parent);
    if (mw != nullptr) {
        QObject::connect(this, &CodeEditor::escPressed, mw, &MainWindow::closeFindWidget);
    }

    setAcceptDrops(false);
    installEventFilter(this);
}

void CodeEditor::loadContents()
{
    static_cast<IDE*>(qApp)->loadLargeFile(filepath,this);
}

void CodeEditor::insertCompletion(const QString &completion)
{
    QTextCursor tc = textCursor();
    int extra = completion.length() - completer->completionPrefix().length();
    tc.movePosition(QTextCursor::Left);
    tc.movePosition(QTextCursor::EndOfWord);
    tc.insertText(completion.right(extra));
    setTextCursor(tc);
}

void CodeEditor::loadedLargeFile()
{
    setReadOnly(false);
    delete loadContentsButton;
    loadContentsButton = nullptr;
}

void CodeEditor::setDocument(QTextDocument *document)
{
    if (document) {
        delete highlighter;
        highlighter = nullptr;
    }
    disconnect(this, &QPlainTextEdit::blockCountChanged, this, &CodeEditor::setViewportWidth);
    disconnect(this, &QPlainTextEdit::updateRequest, this, &CodeEditor::setLineNumbers);
    disconnect(this, &QPlainTextEdit::updateRequest, this, &CodeEditor::setDebugInfoPos);
    disconnect(this, &QPlainTextEdit::cursorPositionChanged, this, &CodeEditor::cursorChange);
    disconnect(this->document(), &QTextDocument::modificationChanged, this, &CodeEditor::docChanged);
    QList<QTextEdit::ExtraSelection> noSelections;
    setExtraSelections(noSelections);
    QPlainTextEdit::setDocument(document);
    if (document) {
        QFont f= font();
        highlighter = new Highlighter(f,darkMode,document);
    }
    connect(this, &QPlainTextEdit::blockCountChanged, this, &CodeEditor::setViewportWidth);
    connect(this, &QPlainTextEdit::updateRequest, this, &CodeEditor::setLineNumbers);
    connect(this, &QPlainTextEdit::updateRequest, this, &CodeEditor::setDebugInfoPos);
    connect(this, &QPlainTextEdit::cursorPositionChanged, this, &CodeEditor::cursorChange);
    connect(this->document(), &QTextDocument::modificationChanged, this, &CodeEditor::docChanged);
}

void CodeEditor::setDarkMode(bool enable)
{
    darkMode = enable;
    highlighter->setDarkMode(enable);
    highlighter->rehighlight();

//// Palettes stop working when stylesheets are used for the same object.
//// Since dark-mode is handeled via css on non-macOS systems, the more efficient pallet code cannot be used.
//// Instead we have to generate css on the fly.
//    auto palette = this->palette();
//    palette.setColor(QPalette::Text, Themes::currentTheme.textColor.get(darkMode));
//    palette.setColor(QPalette::Base, Themes::currentTheme.backgroundColor.get(darkMode));
//    palette.setColor(QPalette::Highlight, Themes::currentTheme.textHighlightColor.get(darkMode));
//    palette.setColor(QPalette::HighlightedText, Themes::currentTheme.textColor.get(darkMode));
//    this->setPalette(palette);

    viewport()->setStyleSheet(Themes::currentTheme.styleSheet(darkMode));
    cursorChange(); // Ensure extra selections are the correct colours
}

void CodeEditor::setIndentSize(int size)
{
    indentSize = size;
    QFontMetrics metrics(font());
    setTabStopDistance(metrics.horizontalAdvance(' ') * size);
}

Highlighter& CodeEditor::getHighlighter() {
    return *highlighter;
}

void CodeEditor::docChanged(bool c)
{
    int t = tabs == nullptr ? -1 : tabs->indexOf(this);
    if (t != -1) {
        QString title = tabs->tabText(t);
        title = title.mid(0, title.lastIndexOf(" *"));
        if (c)
            title += " *";
        tabs->setTabText(t,title);
    }
}

void CodeEditor::contentsChanged()
{
    modificationTimer.start(500);
}

void CodeEditor::contentsChangedWithTimeout()
{
    emit changedDebounced();
}

void CodeEditor::keyPressEvent(QKeyEvent *e)
{
    if (completer->popup()->isVisible()) {
        switch (e->key()) {
        case Qt::Key_Enter:
        case Qt::Key_Return:
        case Qt::Key_Escape:
        case Qt::Key_Tab:
        case Qt::Key_Backtab:
            e->ignore();
            return; // let the completer do default behavior
        default:
            break;
        }
    }
    if (e->key() == Qt::Key_Backtab) {
        e->accept();
        shiftLeft();
        ensureCursorVisible();
    } else if (e->key() == Qt::Key_Tab) {
        e->accept();
        auto cursor = textCursor();
        auto startBlock = document()->findBlock(cursor.selectionStart());
        auto endBlock = document()->findBlock(cursor.selectionEnd());
        auto partialLineSelection = startBlock.position() != cursor.selectionStart()
                || endBlock.position() + endBlock.length() - 1 != cursor.selectionEnd();
        if (!cursor.hasSelection() || (startBlock == endBlock && partialLineSelection)) {
            if (useTabs) {
                cursor.insertText("\t");
            } else {
                auto posInLine = cursor.selectionStart() - startBlock.position();
                auto distanceFromTabStop = 0;
                auto line = startBlock.text();
                for (int i = 0; i < posInLine; i++) {
                    if (line[i] == '\t') {
                        distanceFromTabStop = 0;
                    } else if (line[i].isSpace()) {
                        distanceFromTabStop++;
                    } else {
                        break;
                    }
                }
                auto toAdd = distanceFromTabStop % indentSize;
                if (toAdd == 0) {
                    toAdd = indentSize;
                }
                cursor.insertText(QString(" ").repeated(toAdd));
            }
        } else {
            shiftRight();
        }
        ensureCursorVisible();
    } else if (e->key() == Qt::Key_Return) {
        e->accept();
        QTextCursor cursor(textCursor());
        QString curLine = cursor.block().text();
        QRegularExpression leadingWhitespace("^(\\s*)");
        cursor.insertText("\n");
        auto leadingWhitespace_match = leadingWhitespace.match(curLine);
        if (leadingWhitespace_match.hasMatch()) {
            cursor.insertText(leadingWhitespace_match.captured(1));
        }
        ensureCursorVisible();
    } else if (e->key() == Qt::Key_Escape) {
        e->accept();
        emit escPressed();
    } else {
        bool isShortcut = ((e->modifiers() & Qt::ControlModifier) && e->key() == Qt::Key_E); // CTRL+E
        if (!isShortcut) // do not process the shortcut when we have a completer
            QPlainTextEdit::keyPressEvent(e);
        const bool ctrlOrShift = e->modifiers() & (Qt::ControlModifier | Qt::ShiftModifier);
        if (ctrlOrShift && e->text().isEmpty())
            return;
        static QString eow("~!@#$%^&*()_+{}|:\"<>?,./;'[]\\-="); // end of word
        bool hasModifier = (e->modifiers() != Qt::NoModifier) && !ctrlOrShift;

        QTextCursor tc = textCursor();
        tc.select(QTextCursor::WordUnderCursor);
        QString completionPrefix = tc.selectedText();
        if (!isShortcut && (hasModifier || e->text().isEmpty()|| completionPrefix.length() < 3
                            || eow.contains(e->text().right(1)))) {
            completer->popup()->hide();
            return;
        }
        if (completionPrefix != completer->completionPrefix()) {
            completer->setCompletionPrefix(completionPrefix);
            completer->popup()->setCurrentIndex(completer->completionModel()->index(0, 0));
        }
        QRect cr = cursorRect();
        cr.setWidth(completer->popup()->sizeHintForColumn(0)
                    + completer->popup()->verticalScrollBar()->sizeHint().width());
        completer->complete(cr); // popup it up!
    }
}

int CodeEditor::lineNumbersWidth()
{
    int width = 1;
    int bc = blockCount();
    while (bc >= 10) {
        bc /= 10;
        ++width;
    }
    width = std::max(width,3);
    return 3 + fontMetrics().boundingRect(QLatin1Char('9')).width() * width;
}

int CodeEditor::debugInfoWidth()
{
    return !debugInfo->isVisible()?0:(3*DEBUG_TAB_SIZE);
}

int CodeEditor::debugInfoOffset()
{
    int heightOffset = 0;
    if(debugInfo->isVisible()){
        QFont lineNoFont = font();
        QFontMetrics fm(lineNoFont);
        heightOffset = fm.height();
    }
    return heightOffset;
}

void CodeEditor::showDebugInfo(bool show)
{
    if (filepath!="" && !filepath.endsWith(".mzn"))
        show = false;
    if (show) {
        if(debugInfo->isHidden()){
            debugInfo->show();
            setViewportWidth(0);
        }
    } else {
        if(!debugInfo->isHidden()){
            debugInfo->hide();
            setViewportWidth(0);
        }
    }
}

void CodeEditor::setViewportWidth(int)
{
    setViewportMargins(lineNumbersWidth(), debugInfoOffset(), debugInfoWidth(), 0);
}



void CodeEditor::setLineNumbers(const QRect &rect, int dy)
{
    if (dy)
        lineNumbers->scroll(0, dy);
    else
        lineNumbers->update(0, rect.y(), lineNumbers->width(), rect.height());

    if (rect.contains(viewport()->rect()))
        setViewportWidth(0);
}

void CodeEditor::setDebugInfoPos(const QRect &rect, int dy)
{
    if (dy)
        debugInfo->scroll(0, dy);
    else
        debugInfo->update(0, rect.y(), debugInfo->width(), rect.height());


    if (rect.contains(viewport()->rect()))
        setViewportWidth(0);
}





void CodeEditor::resizeEvent(QResizeEvent *e)
{
    QPlainTextEdit::resizeEvent(e);

    QRect cr = contentsRect();
    lineNumbers->setGeometry(QRect(cr.left(), cr.top()+debugInfoOffset(), lineNumbersWidth(), cr.height()));
    if (loadContentsButton) {
        loadContentsButton->move(cr.left()+lineNumbersWidth(), cr.top());
    }

    debugInfo->setGeometry(QRect(cr.right()-debugInfoWidth(), cr.top()+debugInfoOffset(), debugInfoWidth(), cr.height()));

    editorHeader->setGeometry(QRect(cr.left(), cr.top(), cr.width(), debugInfoOffset()));
}

void CodeEditor::showEvent(QShowEvent *event)
{
    setViewportWidth(0);
}



void CodeEditor::cursorChange()
{
    QList<QTextEdit::ExtraSelection> allExtraSels = extraSelections();

    BracketData* bd = static_cast<BracketData*>(textCursor().block().userData());
    QList<QTextEdit::ExtraSelection> extraSelections;

    {
        QTextEdit::ExtraSelection highlightLineSelection;
        QColor lineColor = Themes::currentTheme.lineHighlightColor.get(darkMode);
        highlightLineSelection.format.setBackground(lineColor);
        highlightLineSelection.format.setProperty(QTextFormat::FullWidthSelection, true);
        highlightLineSelection.cursor = textCursor();
        highlightLineSelection.cursor.clearSelection();
        extraSelections.append(highlightLineSelection);
    }

    auto ec = Themes::currentTheme.errorColor.get(darkMode);
    auto wc = Themes::currentTheme.warningColor.get(darkMode);
    foreach (QTextEdit::ExtraSelection sel, allExtraSels) {
        if (sel.format.underlineColor() == ec || sel.format.underlineColor() == wc) {
            extraSelections.append(sel);
        }
    }

    if (bd) {
        QVector<Bracket>& brackets = bd->brackets;
        int pos = textCursor().block().position();
        for (int i=0; i<brackets.size(); i++) {
            int curPos = textCursor().position()-textCursor().block().position();
            Bracket& b = brackets[i];
            int parenPos0 = -1;
            int parenPos1 = -1;
            int errPos = -1;
            if (b.pos == curPos-1 && (b.b == '(' || b.b == '{' || b.b == '[')) {
                parenPos1 = matchLeft(textCursor().block(), b.b, i+1, 0);
                if (parenPos1 != -1) {
                    parenPos0 = pos+b.pos;
                } else {
                    errPos = pos+b.pos;
                }
            } else if (b.pos == curPos-1 && (b.b == ')' || b.b == '}' || b.b == ']')) {
                parenPos0 = matchRight(textCursor().block(), b.b, i-1, 0);
                if (parenPos0 != -1) {
                    parenPos1 = pos+b.pos;
                } else {
                    errPos = pos+b.pos;
                }
            }
            if (parenPos0 != -1 && parenPos1 != -1) {
                QTextEdit::ExtraSelection sel;
                QTextCharFormat format = sel.format;
                format.setBackground(Themes::currentTheme.bracketsMatchColor.get(darkMode));
                sel.format = format;
                QTextCursor cursor = textCursor();
                cursor.setPosition(parenPos0);
                cursor.movePosition(QTextCursor::NextCharacter, QTextCursor::KeepAnchor);
                sel.cursor = cursor;
                extraSelections.append(sel);
                cursor.setPosition(parenPos1);
                cursor.movePosition(QTextCursor::NextCharacter, QTextCursor::KeepAnchor);
                sel.cursor = cursor;
                extraSelections.append(sel);
            }
            if (errPos != -1) {
                QTextEdit::ExtraSelection sel;
                QTextCharFormat format = sel.format;
                format.setBackground(Themes::currentTheme.bracketsNoMatchColor.get(darkMode));
                sel.format = format;
                QTextCursor cursor = textCursor();
                cursor.setPosition(errPos);
                cursor.movePosition(QTextCursor::NextCharacter, QTextCursor::KeepAnchor);
                sel.cursor = cursor;
                extraSelections.append(sel);
            }
        }
    }

    setExtraSelections(extraSelections);
}

int CodeEditor::matchLeft(QTextBlock block, QChar b, int i, int nLeft)
{
    QChar match;
    switch (b.toLatin1()) {
    case '(' : match = ')'; break;
    case '{' : match = '}'; break;
    case '[' : match = ']'; break;
    default: break; // should not happen
    }

    while (block.isValid()) {
        BracketData* bd = static_cast<BracketData*>(block.userData());
        QVector<Bracket>& brackets = bd->brackets;
        int docPos = block.position();
        for (; i<brackets.size(); i++) {
            Bracket& b = brackets[i];
            if (b.b=='(' || b.b=='{' || b.b=='[') {
                nLeft++;
            } else if (b.b==match && nLeft==0) {
                return docPos+b.pos;
            } else {
                nLeft--;
            }
        }
        block = block.next();
        i = 0;
    }
    return -1;
}

int CodeEditor::matchRight(QTextBlock block, QChar b, int i, int nRight)
{
    QChar match;
    switch (b.toLatin1()) {
    case ')' : match = '('; break;
    case '}' : match = '{'; break;
    case ']' : match = '['; break;
    default: break; // should not happen
    }
    if (i==-1)
        block = block.previous();
    while (block.isValid()) {
        BracketData* bd = static_cast<BracketData*>(block.userData());
        QVector<Bracket>& brackets = bd->brackets;
        if (i==-1)
            i = brackets.size()-1;
        int docPos = block.position();
        for (; i>-1 && brackets.size()>0; i--) {
            Bracket& b = brackets[i];
            if (b.b==')' || b.b=='}' || b.b==']') {
                nRight++;
            } else if (b.b==match && nRight==0) {
                return docPos+b.pos;
            } else {
                nRight--;
            }
        }
        block = block.previous();
        i = -1;
    }
    return -1;
}

void CodeEditor::paintLineNumbers(QPaintEvent *event)
{
    QPainter painter(lineNumbers);
    QFont lineNoFont = font();
    QFontMetrics fm(lineNoFont);
    lineNoFont.setPointSizeF(lineNoFont.pointSizeF()*0.8);
    QFontMetrics fm2(lineNoFont);
    int ascentDiff = fontMetrics().ascent() - fm2.ascent();
    painter.setFont(lineNoFont);
    painter.fillRect(event->rect(), Themes::currentTheme.lineNumberbackground.get(darkMode));

    QTextBlock block = firstVisibleBlock();
    int blockNumber = block.blockNumber();

    int top = static_cast<int>(blockBoundingGeometry(block).translated(contentOffset()).top());
    int bottom = top + static_cast<int>(blockBoundingRect(block).height());

//    painter.fillRect(event->rect(), QColor::fromRgb(QRandomGenerator::global()->generate()));

    int curLine = textCursor().blockNumber();

    while (block.isValid() && top <= event->rect().bottom()) {
        if (block.isVisible() && bottom >= event->rect().top()) {
            QString number = QString::number(blockNumber + 1);
            int textTop = top + ascentDiff;

            if (errorLines.contains(blockNumber)) {
                painter.setPen(Themes::currentTheme.errorColor.get(darkMode));
            } else if (warningLines.contains(blockNumber)) {
                painter.setPen(Themes::currentTheme.warningColor.get(darkMode));
            } else if (blockNumber == curLine) {
                painter.setPen(Themes::currentTheme.foregroundActiveColor.get(darkMode));
            } else {
                painter.setPen(Themes::currentTheme.foregroundInactiveColor.get(darkMode));
            }

            painter.drawText(0, textTop, lineNumbers->width(), fm2.height(),
                             Qt::AlignRight, number);
        }

        block = block.next();
        top = bottom;
        bottom = top + static_cast<int>(blockBoundingRect(block).height());
        ++blockNumber;
    }
}

QColor CodeEditor::interpolate(QColor start,QColor end,double ratio)
{
    //From https://stackoverflow.com/questions/3306786/get-intermediate-color-from-a-gradient
    int r = static_cast<int>(ratio*start.red() + (1-ratio)*end.red());
    int g = static_cast<int>(ratio*start.green() + (1-ratio)*end.green());
    int b = static_cast<int>(ratio*start.blue() + (1-ratio)*end.blue());
    int a = static_cast<int>(ratio*start.alpha() + (1-ratio)*end.alpha());
    QColor c = QColor::fromRgb(r,g,b);
    c.setAlpha(a); // Strangely there seems to be no clean way of creating a rgba color, fromRgba does not take 4 parameters as one would expect.
    return c;
}

QColor CodeEditor::heatColor(double ratio)
{
//return interpolate(darkMode?QColor(191, 0, 0):QColor(Qt::red).lighter(110), QColor(Qt::transparent), ratio);
//    QColor bg =  darkMode?QColor(0x26, 0x26, 0x26):Qt::white;
    QColor bg = Themes::currentTheme.backgroundColor.get(darkMode);
    bg.setAlpha(50);
    return interpolate(darkMode?QColor(191, 0, 0):QColor(Qt::red).lighter(110), bg, ratio);
}

void CodeEditor::paintDebugInfo(QPaintEvent *event)
{
    QPainter painter(debugInfo);
    QFont lineNoFont = font();
    QFontMetrics fm(lineNoFont);
    int origFontHeight = fm.height();
    lineNoFont.setPointSizeF(lineNoFont.pointSizeF()*0.8);
    QFontMetrics fm2(lineNoFont);
    int heightDiff = (origFontHeight-fm2.height());
    painter.setFont(lineNoFont);

    painter.fillRect(event->rect(), Themes::currentTheme.backgroundColor.get(darkMode));

    // TODO: This should be pre-computed only once and stored in each block.
    // Statistics should not be recounted at eack redraw...

    QTextBlock block = firstVisibleBlock();
    int blockNumber = block.blockNumber();
    int top = static_cast<int>(blockBoundingGeometry(block).translated(contentOffset()).top());
    int bottom = top + static_cast<int>(blockBoundingRect(block).height());

    int curLine = textCursor().blockNumber();

    painter.setPen(Themes::currentTheme.foregroundActiveColor.get(darkMode));
    while (block.isValid() && top <= event->rect().bottom()) {
        BracketData* bd = static_cast<BracketData*>(block.userData());
        if (block.isVisible() && bottom >= event->rect().top() && bd->d.hasData()) {
//            int textTop = top+fontMetrics().leading()+heightDiff;
            int textTop = top+heightDiff;
            // num constraints
            painter.fillRect(0, top, DEBUG_TAB_SIZE, static_cast<int>(blockBoundingRect(block).height()),
                             heatColor(static_cast<double>(bd->d.con)/bd->d.totalCon));
            QString numConstraints = QString().number(bd->d.con);
            painter.drawText(0, textTop, DEBUG_TAB_SIZE, fm2.height(), Qt::AlignCenter, numConstraints);
            // num vars
            painter.fillRect(DEBUG_TAB_SIZE, top, DEBUG_TAB_SIZE, static_cast<int>(blockBoundingRect(block).height()),
                              heatColor(static_cast<double>(bd->d.var)/bd->d.totalVar));
            QString numVars = QString().number(bd->d.var);
            painter.drawText(DEBUG_TAB_SIZE, textTop, DEBUG_TAB_SIZE, fm2.height(), Qt::AlignCenter, numVars);
            // flatten time
            painter.fillRect(DEBUG_TAB_SIZE*2, top, DEBUG_TAB_SIZE, static_cast<int>(blockBoundingRect(block).height()),
                              heatColor(static_cast<double>(bd->d.ms)/bd->d.totalMs));
            QString flattenTime = QString().number(bd->d.ms);
            painter.drawText(DEBUG_TAB_SIZE*2, textTop, DEBUG_TAB_SIZE, fm2.height(), Qt::AlignCenter, flattenTime+"ms");
            //            painter.drawText(0, textTop, debugInfo->width(), fm2.height(),
//                             Qt::AlignLeft, number);
        }

        block = block.next();
        top = bottom;
        bottom = top + static_cast<int>(blockBoundingRect(block).height());
        ++blockNumber;
    }

    painter.setPen(Themes::currentTheme.foregroundInactiveColor.get(darkMode));
    painter.drawLine(0,0,0,event->rect().bottom());
}

void CodeEditor::paintHeader(QPaintEvent *event)
{
    QPainter painter(editorHeader);
    QFont lineNoFont = font();
    QFontMetrics fm(lineNoFont);
    int origFontHeight = fm.height();
    lineNoFont.setPointSizeF(lineNoFont.pointSizeF()*0.8);
    QFontMetrics fm2(lineNoFont);
    int heightDiff = (origFontHeight-fm2.height());
    painter.setFont(lineNoFont);


    painter.fillRect(event->rect(), Themes::currentTheme.backgroundColor.get(darkMode));
    int baseX = debugInfo->geometry().x();
    painter.setPen(Themes::currentTheme.textColor.get(darkMode));
    painter.drawText(baseX, heightDiff/2, DEBUG_TAB_SIZE, debugInfoOffset(), Qt::AlignCenter, "Cons");
    painter.drawText(baseX + DEBUG_TAB_SIZE, heightDiff/2, DEBUG_TAB_SIZE, debugInfoOffset(), Qt::AlignCenter, "Vars");
    painter.drawText(baseX + DEBUG_TAB_SIZE*2, heightDiff/2, DEBUG_TAB_SIZE, debugInfoOffset(), Qt::AlignCenter, "Time");
}

void CodeEditor::setEditorFont(QFont& font)
{
    setFont(font);
    document()->setDefaultFont(font);
    highlighter->setEditorFont(font);
    setIndentSize(indentSize);
    highlighter->rehighlight();
}

bool CodeEditor::eventFilter(QObject *, QEvent *ev)
{
    if (ev->type() == QEvent::KeyPress) {
        QKeyEvent *keyEvent = static_cast<QKeyEvent*>(ev);
        if (keyEvent == QKeySequence::Copy) {
            copy();
            return true;
        } else if (keyEvent == QKeySequence::Cut) {
            cut();
            return true;
        }
    } else if (ev->type() == QEvent::ToolTip) {
        QHelpEvent* helpEvent = static_cast<QHelpEvent*>(ev);
        QPoint evPos = helpEvent->pos();
        evPos = QPoint(evPos.x()-lineNumbersWidth(),evPos.y());
        if (evPos.x() >= 0) {
            QTextCursor cursor = cursorForPosition(evPos);
            bool foundError = false;
            foreach (CodeEditorError cee, errors) {
                if (cursor.position() >= cee.startPos && cursor.position() <= cee.endPos) {
                    QToolTip::showText(helpEvent->globalPos(), cee.msg);
                    foundError = true;
                    break;
                }
            }
            if (!foundError) {
                cursor.select(QTextCursor::WordUnderCursor);
                QString loc(filename+":");
                loc += QString().number(cursor.block().blockNumber()+1);
                loc += ".";
                loc += QString().number(cursor.selectionStart()-cursor.block().position()+1);
                QHash<QString,QString>::iterator idMapIt = idMap.find(loc);
                if (idMapIt != idMap.end()) {
                    QToolTip::showText(helpEvent->globalPos(), idMapIt.value());
                } else {
                    QToolTip::hideText();
                }
            }
        } else {
            QToolTip::hideText();
        }
        return true;
    }
    return false;
}

void CodeEditor::copy()
{
    highlighter->copyHighlightedToClipboard(textCursor());
}


void CodeEditor::cut()
{
    highlighter->copyHighlightedToClipboard(textCursor());
    textCursor().removeSelectedText();
}

void CodeEditor::checkFile(const QVector<MiniZincError>& mznErrors)
{
    auto errorColor = Themes::currentTheme.errorColor.get(darkMode);
    auto warningColor = Themes::currentTheme.warningColor.get(darkMode);

    QList<QTextEdit::ExtraSelection> allExtraSels = extraSelections();

    QList<QTextEdit::ExtraSelection> extraSelections;
    foreach (QTextEdit::ExtraSelection sel, allExtraSels) {
        if (sel.format.underlineColor() != errorColor && sel.format.underlineColor() != warningColor) {
            extraSelections.append(sel);
        }
    }

    errors.clear();
    errorLines.clear();
    warningLines.clear();
    for (auto& it : mznErrors) {
        QTextEdit::ExtraSelection sel;
        QTextCharFormat format = sel.format;
        format.setUnderlineStyle(QTextCharFormat::WaveUnderline);
        format.setUnderlineColor(it.isWarning ? warningColor : errorColor);
        QTextBlock block = document()->findBlockByNumber(it.first_line-1);
        QTextBlock endblock = document()->findBlockByNumber(it.last_line-1);
        if (block.isValid() && endblock.isValid()) {
            QTextCursor cursor = textCursor();
            cursor.setPosition(block.position());
            int firstCol = it.first_col < it.last_col ? (it.first_col-1) : (it.last_col-1);
            int lastCol = it.first_col < it.last_col ? (it.last_col) : (it.first_col);
            cursor.movePosition(QTextCursor::NextCharacter, QTextCursor::MoveAnchor, firstCol);
            int startPos = cursor.position();
            cursor.setPosition(endblock.position(), QTextCursor::KeepAnchor);
            cursor.movePosition(QTextCursor::NextCharacter, QTextCursor::KeepAnchor, lastCol);
            int endPos = cursor.position();
            sel.cursor = cursor;
            sel.format = format;
            extraSelections.append(sel);
            CodeEditorError cee(startPos, endPos, QString(it.isWarning ? "Warning: %1" : "Error: %1").arg(it.msg));
            errors.append(cee);
            for (int j=it.first_line; j<=it.last_line; j++) {
                if (it.isWarning) {
                    warningLines.insert(j - 1);
                } else {
                    errorLines.insert(j - 1);
                }
            }
        }
    }
    setExtraSelections(extraSelections);
}

void CodeEditor::shiftLeft()
{
    shiftSelected(-1);
}

void CodeEditor::shiftRight()
{
    shiftSelected(1);
}

void CodeEditor::shiftSelected(int amount)
{
    auto origCursor = textCursor();
    origCursor.setKeepPositionOnInsert(true);
    QTextCursor cursor = textCursor();
    QTextBlock block = document()->findBlock(cursor.selectionStart());
    QTextBlock endblock = document()->findBlock(cursor.selectionEnd());
    bool atBlockStart = cursor.selectionEnd() == endblock.position();
    if (block==endblock || !atBlockStart)
        endblock = endblock.next();
    cursor.beginEditBlock();
    do {
        auto position = block.position();
        cursor.setPosition(position);
        auto indentation = 0;
        for (auto c : block.text()) {
            if (c == '\t') {
                indentation = indentSize * ((indentation + indentSize) / indentSize);
            } else if (c.isSpace()) {
                indentation++;
            } else {
                break;
            }
            position++;
        }
        cursor.setPosition(position, QTextCursor::KeepAnchor);
        auto newIndent = std::max(0, indentation + indentSize * amount);
        if (useTabs) {
            cursor.insertText(
                        QString("\t").repeated(newIndent / indentSize) +
                        QString(" ").repeated(newIndent % indentSize));
        } else {
            cursor.insertText(QString(" ").repeated(newIndent));
        }
        block = block.next();
    } while (block.isValid() && block != endblock);
    cursor.endEditBlock();
    origCursor.setKeepPositionOnInsert(false);
    setTextCursor(origCursor);
}
