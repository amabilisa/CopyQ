/*
    Copyright (c) 2016, Lukas Holecek <hluk@email.cz>

    This file is part of CopyQ.

    CopyQ is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    CopyQ is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with CopyQ.  If not, see <http://www.gnu.org/licenses/>.
*/
#include "gui/logdialog.h"
#include "ui_logdialog.h"

#include "common/common.h"
#include "common/log.h"

#include <QCheckBox>
#include <QElapsedTimer>
#include <QRegExp>
#include <QTextBlock>
#include <QTextCharFormat>
#include <QTextBlockFormat>
#include <QTextCursor>
#include <QTimer>

namespace {

void addFilterCheckBox(QLayout *layout, LogLevel level, const char *slot)
{
    QWidget *parent = layout->parentWidget();
    QCheckBox *checkBox = new QCheckBox(parent);
    checkBox->setText(logLevelLabel(level));
    checkBox->setChecked(true);
    QObject::connect(checkBox, SIGNAL(toggled(bool)), parent, slot);
    layout->addWidget(checkBox);
}

void showLogLines(QString *content, bool show, LogLevel level)
{
    if (show)
        return;

    const QString label = logLevelLabel(level);
    QRegExp re("\n" + label + "[^\n]*");
    content->remove(re);
}

} // namespace

/// Decorates document in batches so it doesn't block UI.
class Decorator : public QObject
{
    Q_OBJECT
public:
    Decorator(const QRegExp &re, QObject *parent)
        : QObject(parent)
        , m_re(re)
    {
        initSingleShotTimer(&m_timerDecorate, 0, this, SLOT(decorateBatch()));
    }

    /// Start decorating.
    void decorate(QTextDocument *document)
    {
        m_tc = QTextCursor(document);
        m_tc.movePosition(QTextCursor::End);
        decorateBatch();
    }

private slots:
    void decorateBatch()
    {
        if (m_tc.isNull())
            return;

        QElapsedTimer t;
        t.start();

        do {
            m_tc = m_tc.document()->find(m_re, m_tc, QTextDocument::FindBackward);
            if (m_tc.isNull())
                return;

            decorate(&m_tc);
        } while ( t.elapsed() < 20 );

        m_timerDecorate.start();
    }

private:
    virtual void decorate(QTextCursor *tc) = 0;

    QTimer m_timerDecorate;
    QTextCursor m_tc;
    QRegExp m_re;
};

class LogDecorator : public Decorator
{
public:
    LogDecorator(QFont font, QObject *parent)
        : Decorator(QRegExp("^.*: "), parent)
        , m_labelNote(logLevelLabel(LogNote))
        , m_labelError(logLevelLabel(LogError))
        , m_labelWarning(logLevelLabel(LogWarning))
        , m_labelDebug(logLevelLabel(LogDebug))
        , m_labelTrace(logLevelLabel(LogTrace))
    {
        QFont boldFont = font;
        boldFont.setBold(true);

        QTextCharFormat normalFormat;
        normalFormat.setFont(boldFont);
        normalFormat.setBackground(Qt::white);
        normalFormat.setForeground(Qt::black);

        m_noteLogLevelFormat = normalFormat;

        m_errorLogLevelFormat = normalFormat;
        m_errorLogLevelFormat.setForeground(Qt::red);

        m_warningLogLevelFormat = normalFormat;
        m_warningLogLevelFormat.setForeground(Qt::darkRed);

        m_debugLogLevelFormat = normalFormat;
        m_debugLogLevelFormat.setForeground(QColor(100, 100, 200));

        m_traceLogLevelFormat = normalFormat;
        m_traceLogLevelFormat.setForeground(QColor(200, 150, 100));
    }

private:
    void decorate(QTextCursor *tc)
    {
        const QString text = tc->selectedText();
        if ( text.startsWith(m_labelNote) )
            tc->setCharFormat(m_noteLogLevelFormat);
        else if ( text.startsWith(m_labelError) )
            tc->setCharFormat(m_errorLogLevelFormat);
        else if ( text.startsWith(m_labelWarning) )
            tc->setCharFormat(m_warningLogLevelFormat);
        else if ( text.startsWith(m_labelDebug) )
            tc->setCharFormat(m_debugLogLevelFormat);
        else if ( text.startsWith(m_labelTrace) )
            tc->setCharFormat(m_traceLogLevelFormat);
    }

    QString m_labelNote;
    QString m_labelError;
    QString m_labelWarning;
    QString m_labelDebug;
    QString m_labelTrace;

    QTextCharFormat m_noteLogLevelFormat;
    QTextCharFormat m_errorLogLevelFormat;
    QTextCharFormat m_warningLogLevelFormat;
    QTextCharFormat m_debugLogLevelFormat;
    QTextCharFormat m_traceLogLevelFormat;
};

class StringDecorator : public Decorator
{
public:
    explicit StringDecorator(QObject *parent)
        : Decorator(QRegExp("\"[^\"]*\"|'[^']*'"), parent)
    {
        m_stringFormat.setForeground(Qt::darkGreen);
    }

private:
    void decorate(QTextCursor *tc)
    {
        tc->setCharFormat(m_stringFormat);
    }

    QTextCharFormat m_stringFormat;
};

LogDialog::LogDialog(QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::LogDialog)
    , m_showError(true)
    , m_showWarning(true)
    , m_showNote(true)
    , m_showDebug(true)
    , m_showTrace(true)
{
    ui->setupUi(this);

    QFont font("Monospace");
    ui->textBrowserLog->setFont(font);

    m_logDecorator = new LogDecorator(ui->textBrowserLog->font(), this);
    m_stringDecorator = new StringDecorator(this);

    addFilterCheckBox(ui->layoutFilters, LogError, SLOT(showError(bool)));
    addFilterCheckBox(ui->layoutFilters, LogWarning, SLOT(showWarning(bool)));
    addFilterCheckBox(ui->layoutFilters, LogNote, SLOT(showNote(bool)));
    addFilterCheckBox(ui->layoutFilters, LogDebug, SLOT(showDebug(bool)));
    addFilterCheckBox(ui->layoutFilters, LogTrace, SLOT(showTrace(bool)));
    ui->layoutFilters->addStretch(1);

    updateLog();
}

LogDialog::~LogDialog()
{
    delete ui;
}

void LogDialog::updateLog()
{
    QString content = readLogFile();
    content.replace("\nCopyQ ", "\n");

    showLogLines(&content, m_showError, LogError);
    showLogLines(&content, m_showWarning, LogWarning);
    showLogLines(&content, m_showNote, LogNote);
    showLogLines(&content, m_showDebug, LogDebug);
    showLogLines(&content, m_showTrace, LogTrace);

    ui->textBrowserLog->setPlainText(content);

    ui->textBrowserLog->moveCursor(QTextCursor::End);

    QTextDocument *doc = ui->textBrowserLog->document();
    m_logDecorator->decorate(doc);
    m_stringDecorator->decorate(doc);
}

void LogDialog::showError(bool show)
{
    m_showError = show;
    updateLog();
}

void LogDialog::showWarning(bool show)
{
    m_showWarning = show;
    updateLog();
}

void LogDialog::showNote(bool show)
{
    m_showNote = show;
    updateLog();
}

void LogDialog::showDebug(bool show)
{
    m_showDebug = show;
    updateLog();
}

void LogDialog::showTrace(bool show)
{
    m_showTrace = show;
    updateLog();
}

#include "logdialog.moc"
