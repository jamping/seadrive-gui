#include <QtGlobal>

#include <QtWidgets>
#include <QApplication>
#include <QDesktopServices>
#include <QFile>
#include <QTextStream>
#include <QDir>
#include <QCoreApplication>
#include <QMessageBox>
#include <QTimer>
#include <QHostInfo>
#include <QMainWindow>

#include <errno.h>
#include <glib.h>

#include "utils/utils.h"
#include "utils/file-utils.h"
#include "utils/log.h"
#include "ui/tray-icon.h"
#include "src/daemon-mgr.h"

#if defined(Q_OS_MAC)
#include "utils/utils-mac.h"
#endif

#include "seadrive-gui.h"

namespace {

#if defined(Q_OS_WIN32)
    const char *kSeadriveDirName = "seadrive";
#else
    const char *kSeadriveDirName = ".seadrive";
#endif

enum DEBUG_LEVEL {
  DEBUG = 0,
  WARNING
};

// -DQT_NO_DEBUG is used with cmake and qmake if it is a release build
// if it is debug build, use DEBUG level as default
#if !defined(QT_NO_DEBUG) || !defined(NDEBUG)
DEBUG_LEVEL seafile_client_debug_level = DEBUG;
#else
// if it is release build, use WARNING level as default
DEBUG_LEVEL seafile_client_debug_level = WARNING;
#endif

void myLogHandlerDebug(QtMsgType type, const QMessageLogContext &context, const QString &msg)
{
    QByteArray localMsg = msg.toLocal8Bit();
    switch (type) {
// Note: By default, this information (QMessageLogContext) is recorded only in debug builds.
// You can overwrite this explicitly by defining QT_MESSAGELOGCONTEXT or QT_NO_MESSAGELOGCONTEXT.
// from http://doc.qt.io/qt-5/qmessagelogcontext.html
#ifdef QT_MESSAGELOGCONTEXT
    case QtDebugMsg:
        g_debug("%s (%s:%u)\n", localMsg.constData(), context.file, context.line);
        break;
    case QtInfoMsg:
        g_info("%s (%s:%u)\n", localMsg.constData(), context.file, context.line);
        break;
    case QtWarningMsg:
        g_warning("%s (%s:%u)\n", localMsg.constData(), context.file, context.line);
        break;
    case QtCriticalMsg:
        g_critical("%s (%s:%u)\n", localMsg.constData(), context.file, context.line);
        break;
    case QtFatalMsg:
        g_critical("%s (%s:%u)\n", localMsg.constData(), context.file, context.line);
        abort();
#else // QT_MESSAGELOGCONTEXT
    case QtDebugMsg:
        g_debug("%s\n", localMsg.constData());
        break;
    case QtInfoMsg:
        g_info("%s\n", localMsg.constData());
        break;
    case QtWarningMsg:
        g_warning("%s\n", localMsg.constData());
        break;
    case QtCriticalMsg:
        g_critical("%s\n", localMsg.constData());
        break;
    case QtFatalMsg:
        g_critical("%s\n", localMsg.constData());
        abort();
#endif // QT_MESSAGELOGCONTEXT
    default:
        break;
    }
}
void myLogHandler(QtMsgType type, const QMessageLogContext &context, const QString &msg)
{
    QByteArray localMsg = msg.toLocal8Bit();
    switch (type) {
#ifdef QT_MESSAGELOGCONTEXT
    case QtInfoMsg:
        g_info("%s (%s:%u)\n", localMsg.constData(), context.file, context.line);
        break;
    case QtWarningMsg:
        g_warning("%s (%s:%u)\n", localMsg.constData(), context.file, context.line);
        break;
    case QtCriticalMsg:
        g_critical("%s (%s:%u)\n", localMsg.constData(), context.file, context.line);
        break;
    case QtFatalMsg:
        g_critical("%s (%s:%u)\n", localMsg.constData(), context.file, context.line);
        abort();
#else // QT_MESSAGELOGCONTEXT
    case QtInfoMsg:
        g_info("%s\n", localMsg.constData());
        break;
    case QtWarningMsg:
        g_warning("%s\n", localMsg.constData());
        break;
    case QtCriticalMsg:
        g_critical("%s\n", localMsg.constData());
        break;
    case QtFatalMsg:
        g_critical("%s\n", localMsg.constData());
        abort();
#endif // QT_MESSAGELOGCONTEXT
    default:
        break;
    }
}


#ifdef Q_OS_MAC
void writeCABundleForCurl()
{
    // QString ca_bundle_path = QDir(gui->configurator()->seafileDir()).filePath("ca-bundle.pem");
    // QFile bundle(ca_bundle_path);
    // if (bundle.exists()) {
    //     bundle.remove();
    // }
    // bundle.open(QIODevice::WriteOnly);
    // const std::vector<QByteArray> certs = utils::mac::getSystemCaCertificates();
    // for (size_t i = 0; i < certs.size(); i++) {
    //     QList<QSslCertificate> list = QSslCertificate::fromData(certs[i], QSsl::Der);
    //     foreach (const QSslCertificate& cert, list) {
    //         bundle.write(cert.toPem());
    //     }
    // }
}
#endif


// const char *const kPreconfigureUsername = "PreconfigureUsername";
// const char *const kPreconfigureUserToken = "PreconfigureUserToken";
// const char *const kPreconfigureServerAddr = "PreconfigureServerAddr";
// const char *const kPreconfigureComputerName = "PreconfigureComputerName";
// const char* const kHideConfigurationWizard = "HideConfigurationWizard";

#if defined(Q_OS_WIN32)
const char *const kSeafileConfigureFileName = "seafile.ini";
const char *const kSeafileConfigurePath = "SOFTWARE\\Seafile";
const int kIntervalBeforeShowInitVirtualDialog = 3000;
#else
const char *const kSeafileConfigureFileName = ".seafilerc";
#endif
const char *const kSeafilePreconfigureGroupName = "preconfigure";

} // namespace


SeadriveGui *gui;

SeadriveGui::SeadriveGui()
    : started_(false),
      in_exit_(false),
      is_pro_(false)
{
    main_win_ = nullptr;
    tray_icon_ = new SeafileTrayIcon(this);
    daemon_mgr_ = new DaemonManager();
    connect(qApp, SIGNAL(aboutToQuit()), this, SLOT(onAboutToQuit()));
}

SeadriveGui::~SeadriveGui()
{
    delete tray_icon_;
    delete daemon_mgr_;
}

void SeadriveGui::start()
{
    initLog();

    refreshQss();

    qInfo("seadrive gui started");

#if defined(Q_OS_MAC)
    writeCABundleForCurl();
#endif

    daemon_mgr_->startSeadriveDaemon();
    connect(daemon_mgr_, SIGNAL(daemonStarted()),
            this, SLOT(onDaemonStarted()));
}

void SeadriveGui::onDaemonStarted()
{
    tray_icon_->start();
    tray_icon_->setState(SeafileTrayIcon::STATE_DAEMON_UP);
}

void SeadriveGui::onAboutToQuit()
{
    tray_icon_->hide();
}

// stop the main event loop and return to the main function
void SeadriveGui::errorAndExit(const QString& error)
{
    if (in_exit_ || QCoreApplication::closingDown()) {
        return;
    }

    qWarning("Exiting with error: %s", toCStr(error));

    in_exit_ = true;

    warningBox(error);
    // stop eventloop before exit and return to the main function
    QCoreApplication::exit(1);
}

void SeadriveGui::restartApp()
{
    if (in_exit_ || QCoreApplication::closingDown()) {
        return;
    }

    in_exit_ = true;

    QStringList args = QApplication::arguments();

    args.removeFirst();

    // append delay argument
    bool found = false;
    Q_FOREACH(const QString& arg, args)
    {
        if (arg == "--delay" || arg == "-D") {
            found = true;
            break;
        }
    }

    if (!found)
        args.push_back("--delay");

    QProcess::startDetached(QApplication::applicationFilePath(), args);
    QCoreApplication::quit();
}

void SeadriveGui::initLog()
{
    QDir seadrive_dir = seadriveDir();
    if (checkdir_with_mkdir(toCStr(seadrive_dir.absolutePath())) < 0) {
        errorAndExit(tr("Failed to initialize: failed to create seadrive folder"));
        return;
    }
    if (checkdir_with_mkdir(toCStr(logsDir())) < 0) {
        errorAndExit(tr("Failed to initialize: failed to create seadrive logs folder"));
        return;
    }
    if (checkdir_with_mkdir(toCStr(seadriveDataDir())) < 0) {
        errorAndExit(tr("Failed to initialize: failed to create seadrive data folder"));
        return;
    }

    if (applet_log_init(toCStr(seadrive_dir.absolutePath())) < 0) {
        errorAndExit(tr("Failed to initialize log: %s").arg(g_strerror(errno)));
    }

    // give a change to override DEBUG_LEVEL by environment
    QString debug_level = qgetenv("SEAFILE_CLIENT_DEBUG");
    if (!debug_level.isEmpty() && debug_level != "false" &&
        debug_level != "0")
        seafile_client_debug_level = DEBUG;

    if (seafile_client_debug_level == DEBUG)
        qInstallMessageHandler(myLogHandlerDebug);
    else
        qInstallMessageHandler(myLogHandler);
}

bool SeadriveGui::loadQss(const QString& path)
{
    QFile file(path);
    if (!QFileInfo(file).exists()) {
        return false;
    }
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return false;
    }

    QTextStream input(&file);
    style_ += "\n";
    style_ += input.readAll();
    qApp->setStyleSheet(style_);

    return true;
}

void SeadriveGui::refreshQss()
{
    style_.clear();
    loadQss("qt.css") || loadQss(":/qt.css");

#if defined(Q_OS_WIN32)
    loadQss("qt-win.css") || loadQss(":/qt-win.css");
#elif defined(Q_OS_LINUX)
    loadQss("qt-linux.css") || loadQss(":/qt-linux.css");
#else
    loadQss("qt-mac.css") || loadQss(":/qt-mac.css");
#endif
}

void SeadriveGui::warningBox(const QString& msg, QWidget *parent)
{
    QMessageBox box(parent ? parent : main_win_);
    box.setText(msg);
    box.setWindowTitle(getBrand());
    box.setIcon(QMessageBox::Warning);
    box.addButton(tr("OK"), QMessageBox::YesRole);
    box.exec();

    if (!parent) {
        // main_win_->showWindow();
    }
    qWarning("%s", msg.toUtf8().data());
}

void SeadriveGui::messageBox(const QString& msg, QWidget *parent)
{
    QMessageBox box(parent ? parent : main_win_);
    box.setText(msg);
    box.setWindowTitle(getBrand());
    box.setIcon(QMessageBox::Information);
    box.addButton(tr("OK"), QMessageBox::YesRole);
    box.exec();
    qDebug("%s", msg.toUtf8().data());
}

bool SeadriveGui::yesOrNoBox(const QString& msg, QWidget *parent, bool default_val)
{
    QMessageBox box(parent ? parent : main_win_);
    box.setText(msg);
    box.setWindowTitle(getBrand());
    box.setIcon(QMessageBox::Question);
    QPushButton *yes_btn = box.addButton(tr("Yes"), QMessageBox::YesRole);
    QPushButton *no_btn = box.addButton(tr("No"), QMessageBox::NoRole);
    box.setDefaultButton(default_val ? yes_btn: no_btn);
    box.exec();

    return box.clickedButton() == yes_btn;
}

bool SeadriveGui::yesOrCancelBox(const QString& msg, QWidget *parent, bool default_yes)
{
    QMessageBox box(parent ? parent : main_win_);
    box.setText(msg);
    box.setWindowTitle(getBrand());
    box.setIcon(QMessageBox::Question);
    QPushButton *yes_btn = box.addButton(tr("Yes"), QMessageBox::YesRole);
    QPushButton *cancel_btn = box.addButton(tr("Cancel"), QMessageBox::RejectRole);
    box.setDefaultButton(default_yes ? yes_btn: cancel_btn);
    box.exec();

    return box.clickedButton() == yes_btn;
}


QMessageBox::StandardButton
SeadriveGui::yesNoCancelBox(const QString& msg, QWidget *parent, QMessageBox::StandardButton default_btn)
{
    QMessageBox box(parent ? parent : main_win_);
    box.setText(msg);
    box.setWindowTitle(getBrand());
    box.setIcon(QMessageBox::Question);
    QPushButton *yes_btn = box.addButton(tr("Yes"), QMessageBox::YesRole);
    QPushButton *no_btn = box.addButton(tr("No"), QMessageBox::NoRole);
    box.addButton(tr("Cancel"), QMessageBox::RejectRole);
    box.setDefaultButton(default_btn);
    box.exec();

    QAbstractButton *btn = box.clickedButton();
    if (btn == yes_btn) {
        return QMessageBox::Yes;
    } else if (btn == no_btn) {
        return QMessageBox::No;
    }

    return QMessageBox::Cancel;
}

bool SeadriveGui::detailedYesOrNoBox(const QString& msg, const QString& detailed_text, QWidget *parent, bool default_val)
{
    QMessageBox msgBox(QMessageBox::Question,
                       getBrand(),
                       msg,
                       QMessageBox::Yes | QMessageBox::No,
                       parent != 0 ? parent : main_win_);
    msgBox.setDetailedText(detailed_text);
    msgBox.setButtonText(QMessageBox::Yes, tr("Yes"));
    msgBox.setButtonText(QMessageBox::No, tr("No"));
    // Turns out the layout box in the QMessageBox is a grid
    // You can force the resize using a spacer this way:
    QSpacerItem* horizontalSpacer = new QSpacerItem(400, 0, QSizePolicy::Minimum, QSizePolicy::Expanding);
    QGridLayout* layout = (QGridLayout*)msgBox.layout();
    layout->addItem(horizontalSpacer, layout->rowCount(), 0, 1, layout->columnCount());
    msgBox.setDefaultButton(default_val ? QMessageBox::Yes : QMessageBox::No);
    return msgBox.exec() == QMessageBox::Yes;
}

QVariant SeadriveGui::readPreconfigureEntry(const QString& key, const QVariant& default_value)
{
#ifdef Q_OS_WIN32
    QVariant v = RegElement::getPreconfigureValue(key);
    if (!v.isNull()) {
        return v;
    }
#endif
    QString configure_file = QDir::home().filePath(kSeafileConfigureFileName);
    if (!QFileInfo(configure_file).exists())
        return default_value;
    QSettings setting(configure_file, QSettings::IniFormat);
    setting.beginGroup(kSeafilePreconfigureGroupName);
    QVariant value = setting.value(key, default_value);
    setting.endGroup();
    return value;
}

QString SeadriveGui::readPreconfigureExpandedString(const QString& key, const QString& default_value)
{
    QVariant retval = readPreconfigureEntry(key, default_value);
    if (retval.isNull() || retval.type() != QVariant::String)
        return QString();
    return expandVars(retval.toString());
}

QString SeadriveGui::seadriveDir() const
{
    return QDir::home().absoluteFilePath(kSeadriveDirName);
}

QString SeadriveGui::seadriveDataDir() const
{
    return QDir(seadriveDir()).filePath("data");
}

QString SeadriveGui::logsDir() const
{
    return QDir(seadriveDir()).filePath("logs");
}