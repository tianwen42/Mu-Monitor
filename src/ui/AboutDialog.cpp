#include "ui/AboutDialog.h"

#include <QApplication>
#include <QClipboard>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QStyle>
#include <QTabWidget>
#include <QTextBrowser>
#include <QVBoxLayout>
#include <QSysInfo>
#include <QtGlobal>

namespace {
QString compilerName()
{
#if defined(__clang__)
    return QStringLiteral("Clang ") + QString::fromLatin1(__clang_version__);
#elif defined(__GNUC__)
    return QStringLiteral("GCC ") + QString::fromLatin1(__VERSION__);
#elif defined(_MSC_VER)
    return QStringLiteral("MSVC %1").arg(_MSC_VER);
#else
    return QStringLiteral("Unknown");
#endif
}
}

AboutDialog::AboutDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(QStringLiteral("关于 Mu-Monitor"));
    setMinimumSize(680, 520);
    resize(760, 580);

    auto *rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(18, 18, 18, 14);
    rootLayout->setSpacing(12);

    auto *headerLayout = new QHBoxLayout;
    headerLayout->setSpacing(14);

    auto *logo = new QLabel(this);
    const QIcon appIcon = style()->standardIcon(QStyle::SP_ComputerIcon);
    logo->setPixmap(appIcon.pixmap(64, 64));
    logo->setFixedSize(72, 72);
    logo->setAlignment(Qt::AlignCenter);
    logo->setObjectName(QStringLiteral("aboutLogo"));
    headerLayout->addWidget(logo);

    auto *titleLayout = new QVBoxLayout;
    auto *nameLabel = new QLabel(QStringLiteral("Mu-Monitor"), this);
    nameLabel->setObjectName(QStringLiteral("aboutAppName"));
    auto *taglineLabel = new QLabel(QStringLiteral("工业设备监控与告警平台"), this);
    taglineLabel->setObjectName(QStringLiteral("aboutTagline"));
    auto *versionLabel = new QLabel(
        QStringLiteral("Version %1  ·  Qt %2")
            .arg(QApplication::applicationVersion(), QString::fromLatin1(qVersion())), this);
    versionLabel->setObjectName(QStringLiteral("aboutVersion"));
    titleLayout->addWidget(nameLabel);
    titleLayout->addWidget(taglineLabel);
    titleLayout->addWidget(versionLabel);
    headerLayout->addLayout(titleLayout, 1);
    rootLayout->addLayout(headerLayout);

    m_tabs = new QTabWidget(this);
    m_tabs->setObjectName(QStringLiteral("aboutTabs"));

    auto createTextPage = [this](const QString &html, const QString &objectName) {
        auto *browser = new QTextBrowser(this);
        browser->setObjectName(objectName);
        browser->setOpenExternalLinks(true);
        browser->setHtml(html);
        return browser;
    };

    const QString aboutHtml = QStringLiteral(
        "<h3>Mu-Monitor</h3>"
        "<p>面向工业设备数据采集、实时监控、历史存储与告警管理的桌面应用。</p>"
        "<p>项目目标是形成一套完整的工业数据链路：</p>"
        "<p><b>设备模拟器 → TCP/Modbus/MQTT → 协议解析 → 数据管道 → SQLite → 实时界面 → 告警中心</b></p>"
        "<p>当前版本处于界面原型与模拟数据阶段，后续将逐步接入真实设备和工业协议。</p>"
        "<h4>项目定位</h4>"
        "<ul>"
        "<li>工业设备状态与关键参数实时展示</li>"
        "<li>多协议数据接入与统一数据模型</li>"
        "<li>本地历史数据存储与查询</li>"
        "<li>可配置阈值告警与状态机</li>"
        "<li>可扩展的协议与设备驱动接口</li>"
        "</ul>");
    m_tabs->addTab(createTextPage(aboutHtml, QStringLiteral("aboutOverview")), QStringLiteral("关于"));

    const QString featuresHtml = QStringLiteral(
        "<h3>已规划功能</h3>"
        "<ul>"
        "<li><b>设备管理：</b>设备配置、连接状态、启停控制</li>"
        "<li><b>实时监控：</b>温度、压力、转速、电压等遥测数据</li>"
        "<li><b>趋势分析：</b>实时曲线、统计和运行趋势</li>"
        "<li><b>告警中心：</b>阈值规则、抖动过滤、确认与恢复</li>"
        "<li><b>历史数据：</b>SQLite 持久化、时间范围查询、CSV 导出</li>"
        "<li><b>运行日志：</b>系统日志、任务日志和异常记录</li>"
        "<li><b>协议支持：</b>TCP、Modbus TCP/RTU、MQTT、串口</li>"
        "</ul>"
        "<h4>当前已完成</h4>"
        "<ul>"
        "<li>标准 Qt 主窗口、菜单栏和工具栏</li>"
        "<li>总览、实时监控、历史数据 Tab</li>"
        "<li>设备、告警、日志 Dock 面板</li>"
        "<li>模拟设备数据与实时趋势图</li>"
        "<li>独立设置对话框</li>"
        "</ul>");
    m_tabs->addTab(createTextPage(featuresHtml, QStringLiteral("aboutFeatures")), QStringLiteral("功能"));

    const QString techHtml = QStringLiteral(
        "<h3>技术栈</h3>"
        "<table cellspacing='0' cellpadding='5'>"
        "<tr><td><b>语言</b></td><td>C++17 / C++20</td></tr>"
        "<tr><td><b>UI</b></td><td>Qt Widgets</td></tr>"
        "<tr><td><b>Qt</b></td><td>%1</td></tr>"
        "<tr><td><b>构建</b></td><td>CMake + Ninja</td></tr>"
        "<tr><td><b>编译器</b></td><td>%2</td></tr>"
        "<tr><td><b>并发</b></td><td>QThread / QtConcurrent / QThreadPool</td></tr>"
        "<tr><td><b>网络</b></td><td>QTcpSocket / Modbus / MQTT（规划中）</td></tr>"
        "<tr><td><b>存储</b></td><td>SQLite（规划中）</td></tr>"
        "<tr><td><b>日志</b></td><td>QPlainTextEdit / 结构化日志（规划中）</td></tr>"
        "</table>")
        .arg(QString::fromLatin1(qVersion()), compilerName());
    m_tabs->addTab(createTextPage(techHtml, QStringLiteral("aboutTechnology")), QStringLiteral("技术栈"));

    const QString licenseHtml = QStringLiteral(
        "<h3>许可与致谢</h3>"
        "<p>本项目当前用于个人学习、技术验证和作品展示，尚未发布正式开源许可证。</p>"
        "<p>如果后续公开代码，需要补充明确的 LICENSE，并检查所有第三方依赖和参考实现的许可条款。</p>"
        "<h4>界面结构参考</h4>"
        "<ul>"
        "<li><a href='https://github.com/qbittorrent/qBittorrent'>qBittorrent</a>：QMainWindow、工具栏、Dock、设置对话框和 About 对话框</li>"
        "<li><a href='https://github.com/pbek/QOwnNotes'>QOwnNotes</a>：中央工作区、菜单、工具栏和 Dock 面板</li>"
        "<li><a href='https://github.com/michpolicht/CuteHMI'>CuteHMI</a>：工业 HMI 插件式架构</li>"
        "<li><a href='https://github.com/IndeemaSoftware/QSimpleScada'>QSimpleScada</a>：SCADA 仪表盘组件</li>"
        "<li><a href='https://github.com/Serial-Studio/Serial-Studio'>Serial-Studio</a>：现代遥测数据可视化</li>"
        "</ul>"
        "<p>上述项目仅用于研究界面结构和交互方式，本项目没有直接复制其代码。</p>"
        "<h4>Qt</h4>"
        "<p>本项目使用 <a href='https://www.qt.io/'>Qt</a> 开发。Qt 的使用需要遵守对应的开源或商业许可条款。</p>");
    m_tabs->addTab(createTextPage(licenseHtml, QStringLiteral("aboutLicense")), QStringLiteral("许可与致谢"));

    rootLayout->addWidget(m_tabs, 1);

    auto *bottomLayout = new QHBoxLayout;
    auto *copyButton = new QPushButton(QStringLiteral("复制版本信息"), this);
    connect(copyButton, &QPushButton::clicked, this, &AboutDialog::copyBuildInformation);
    bottomLayout->addWidget(copyButton);
    bottomLayout->addStretch();

    auto *buttonBox = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    bottomLayout->addWidget(buttonBox);
    rootLayout->addLayout(bottomLayout);
}

QString AboutDialog::buildInformation() const
{
    return QStringLiteral(
        "Mu-Monitor %1\n"
        "Qt %2\n"
        "Compiler: %3\n"
        "Platform: %4\n"
        "Kernel: %5\n"
        "Build: %6 %7\n")
        .arg(
            QApplication::applicationVersion(),
            QString::fromLatin1(qVersion()),
            compilerName(),
            QSysInfo::prettyProductName(),
            QSysInfo::kernelVersion(),
            QString::fromLatin1(__DATE__),
            QString::fromLatin1(__TIME__));
}

void AboutDialog::copyBuildInformation()
{
    QApplication::clipboard()->setText(buildInformation());
}
