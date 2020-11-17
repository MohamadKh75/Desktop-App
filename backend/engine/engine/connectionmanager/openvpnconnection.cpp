#include "openvpnconnection.h"
#include "utils/crashhandler.h"
#include "utils/logger.h"
#include "utils/utils.h"
#include "engine/types/types.h"
#include "availableport.h"
#include "engine/openvpnversioncontroller.h"

OpenVPNConnection::OpenVPNConnection(QObject *parent, IHelper *helper) : IConnection(parent, helper),
    bStopThread_(false), currentState_(STATUS_DISCONNECTED)
{
    connect(&killControllerTimer_, SIGNAL(timeout()), SLOT(onKillControllerTimer()));
}

OpenVPNConnection::~OpenVPNConnection()
{
    //disconnect();
    wait();
}

void OpenVPNConnection::startConnect(const QString &configPathOrUrl, const QString &ip, const QString &dnsHostName, const QString &username, const QString &password,
                                     const ProxySettings &proxySettings, const WireGuardConfig *wireGuardConfig, bool isEnableIkev2Compression, bool isAutomaticConnectionMode)
{
    Q_UNUSED(ip);
    Q_UNUSED(dnsHostName);
    Q_UNUSED(wireGuardConfig);
    Q_UNUSED(isEnableIkev2Compression);
    Q_UNUSED(isAutomaticConnectionMode);
    Q_ASSERT(getCurrentState() == STATUS_DISCONNECTED);

    qCDebug(LOG_CONNECTION) << "connectOVPN";

    bStopThread_ = true;
    wait();
    bStopThread_ = false;

    setCurrentState(STATUS_CONNECTING);
    configPath_ = configPathOrUrl;
    username_ = username;
    password_ = password;
    proxySettings_ = proxySettings;

    stateVariables_.reset();
    safeSetTapAdapter("");
    start(LowPriority);
}

void OpenVPNConnection::startDisconnect()
{
    if (isDisconnected())
    {
        emit disconnected();
    }
    else
    {
        if (!killControllerTimer_.isActive())
        {
            killControllerTimer_.start(KILL_TIMEOUT);
        }

        bStopThread_ = true;
        io_service_.post(boost::bind( &OpenVPNConnection::funcDisconnect, this ));
    }
}

bool OpenVPNConnection::isDisconnected() const
{
    return getCurrentState() == STATUS_DISCONNECTED;
}

QString OpenVPNConnection::getConnectedTapTunAdapterName()
{
    return safeGetTapAdapter();
}

void OpenVPNConnection::continueWithUsernameAndPassword(const QString &username, const QString &password)
{
    username_ = username;
    password_ = password;
    io_service_.post(boost::bind( &OpenVPNConnection::continueWithUsernameImpl, this ));
}

void OpenVPNConnection::continueWithPassword(const QString &password)
{
    password_ = password;
    io_service_.post(boost::bind( &OpenVPNConnection::continueWithPasswordImpl, this ));
}

void OpenVPNConnection::setCurrentState(CONNECTION_STATUS state)
{
    QMutexLocker locker(&mutexCurrentState_);
    currentState_ = state;
}

void OpenVPNConnection::setCurrentStateAndEmitDisconnected(OpenVPNConnection::CONNECTION_STATUS state)
{
    QMutexLocker locker(&mutexCurrentState_);
    QTimer::singleShot(0, &killControllerTimer_, SLOT(stop()));
    currentState_ = state;
    emit disconnected();
}

void OpenVPNConnection::setCurrentStateAndEmitError(OpenVPNConnection::CONNECTION_STATUS state, CONNECTION_ERROR err)
{
    QMutexLocker locker(&mutexCurrentState_);
    currentState_ = state;
    emit error(err);
}

OpenVPNConnection::CONNECTION_STATUS OpenVPNConnection::getCurrentState() const
{
    QMutexLocker locker(&mutexCurrentState_);
    return currentState_;
}

bool OpenVPNConnection::runOpenVPN(unsigned int port, const ProxySettings &proxySettings, unsigned long &outCmdId)
{
#ifdef Q_OS_WIN
    QString httpProxy, socksProxy;
    unsigned int httpPort = 0, socksPort = 0;

    if (proxySettings.option() == PROXY_OPTION_HTTP)
    {
        httpProxy = proxySettings.address();
        httpPort = proxySettings.getPort();
    }
    else if (proxySettings.option() == PROXY_OPTION_SOCKS)
    {
        socksProxy = proxySettings.address();
        socksPort = proxySettings.getPort();
    }
    else if (proxySettings.option() == PROXY_OPTION_AUTODETECT)
    {
        Q_ASSERT(false);
    }

    qCDebug(LOG_CONNECTION) << "OpenVPN version:" << OpenVpnVersionController::instance().getSelectedOpenVpnVersion();

    return helper_->executeOpenVPN(configPath_, port, httpProxy, httpPort, socksProxy, socksPort, outCmdId);

#elif defined Q_OS_MAC
    QString strCommand = "--config \"" + configPath_ + "\" --management 127.0.0.1 " + QString::number(port) + " --management-query-passwords --management-hold";
    if (proxySettings.option() == PROXY_OPTION_HTTP)
    {
        strCommand += " --http-proxy " + proxySettings.address() + " " + QString::number(proxySettings.getPort()) + " auto";
    }
    else if (proxySettings.option() == PROXY_OPTION_SOCKS)
    {
        strCommand += " --socks-proxy " + proxySettings.address() + " " + QString::number(proxySettings.getPort());
    }
    else if (proxySettings.option() == PROXY_OPTION_AUTODETECT)
    {
        Q_ASSERT(false);
    }
    qCDebug(LOG_CONNECTION) << "OpenVPN version:" << OpenVpnVersionController::instance().getSelectedOpenVpnVersion();
    //qCDebug(LOG_CONNECTION) << strCommand;

    std::wstring strOvpnConfigPath = Utils::getDirPathFromFullPath(configPath_.toStdWString());
    QString qstrOvpnConfigPath = QString::fromStdWString(strOvpnConfigPath);

    return helper_->executeOpenVPN(strCommand, qstrOvpnConfigPath, outCmdId);
#endif
}

void OpenVPNConnection::run()
{
    Debug::CrashHandlerForThread bind_crash_handler_to_this_thread;
    io_service_.reset();
    io_service_.post(boost::bind( &OpenVPNConnection::funcRunOpenVPN, this ));
    io_service_.run();
}

void OpenVPNConnection::onKillControllerTimer()
{
    qCDebug(LOG_CONNECTION) << "openvpn process not finished after " << KILL_TIMEOUT << "ms";
    qCDebug(LOG_CONNECTION) << "kill the openvpn process";
    killControllerTimer_.stop();
#ifdef Q_OS_WIN
    helper_->executeTaskKill(OpenVpnVersionController::instance().getSelectedOpenVpnExecutable());
#elif defined Q_OS_MAC
    helper_->executeRootCommand("pkill -f \"" + OpenVpnVersionController::instance().getSelectedOpenVpnExecutable() + "\"");
#endif
}

void OpenVPNConnection::funcRunOpenVPN()
{
    stateVariables_.openVpnPort = AvailablePort::getAvailablePort(DEFAULT_PORT);

    stateVariables_.elapsedTimer.start();

    int retries = 0;

    // run openvpn process
    while(!runOpenVPN(stateVariables_.openVpnPort, proxySettings_, stateVariables_.lastCmdId))
    {
        qCDebug(LOG_CONNECTION) << "Can't run OpenVPN";
        if (retries >= 2)
        {
            qCDebug(LOG_CONNECTION) << "Can't run openvpn process";
            setCurrentStateAndEmitError(STATUS_DISCONNECTED, CANT_RUN_OPENVPN);
            return;
        }
        if (bStopThread_)
        {
            setCurrentStateAndEmitDisconnected(STATUS_DISCONNECTED);
            return;
        }
        retries++;

        msleep(1000);
    }

    qCDebug(LOG_CONNECTION) << "openvpn process runned: " << stateVariables_.openVpnPort;

    boost::asio::ip::tcp::endpoint endpoint;
    endpoint.port(stateVariables_.openVpnPort);
    endpoint.address(boost::asio::ip::address_v4::from_string("127.0.0.1"));
    stateVariables_.socket.reset(new boost::asio::ip::tcp::socket(io_service_));
    stateVariables_.socket->async_connect(endpoint, boost::bind(&OpenVPNConnection::funcConnectToOpenVPN, this,
                                                                boost::asio::placeholders::error));
}

void OpenVPNConnection::funcConnectToOpenVPN(const boost::system::error_code& err)
{
    if (err.value() == 0)
    {
        qCDebug(LOG_CONNECTION) << "Program connected to openvpn socket";
        helper_->clearUnblockingCmd(stateVariables_.lastCmdId);
        setCurrentState(STATUS_CONNECTED_TO_SOCKET);
        stateVariables_.buffer.reset(new boost::asio::streambuf());
        boost::asio::async_read_until(*stateVariables_.socket, *stateVariables_.buffer, "\n",
                boost::bind(&OpenVPNConnection::handleRead, this,
                  boost::asio::placeholders::error, boost::asio::placeholders::bytes_transferred));

        if (bStopThread_)
        {
            funcDisconnect();
        }
    }
    else
    {
        stateVariables_.socket.reset();

        // check timeout
        if (stateVariables_.elapsedTimer.elapsed() > MAX_WAIT_OPENVPN_ON_START)
        {
            qCDebug(LOG_CONNECTION) << "Can't connect to openvpn socket during"
                                    << (MAX_WAIT_OPENVPN_ON_START/1000) << "secs";
            helper_->clearUnblockingCmd(stateVariables_.lastCmdId);
            setCurrentStateAndEmitError(STATUS_DISCONNECTED, NO_OPENVPN_SOCKET);
            return;
        }

        // check if openvpn process already finished
        QString logStr;
        bool bFinished;
        helper_->getUnblockingCmdStatus(stateVariables_.lastCmdId, logStr, bFinished);

        if (bFinished)
        {
            qCDebug(LOG_CONNECTION) << "openvpn process finished before connected to openvpn socket";
            qCDebug(LOG_CONNECTION) << "answer from openvpn process, answer =" << logStr;

            if (bStopThread_)
            {
                setCurrentStateAndEmitDisconnected(STATUS_DISCONNECTED);
                return;
            }

            //try second attempt to run openvpn after pause 2 sec
            if (!stateVariables_.bWasSecondAttemptToStartOpenVpn)
            {
                qCDebug(LOG_CONNECTION) << "try second attempt to run openvpn after pause 2 sec";
                msleep(2000);
                stateVariables_.bWasSecondAttemptToStartOpenVpn = true;
                io_service_.post(boost::bind( &OpenVPNConnection::funcRunOpenVPN, this ));
                return;
            }
            else
            {
                setCurrentStateAndEmitError(STATUS_DISCONNECTED, NO_OPENVPN_SOCKET);
                return;
            }
        }

        boost::asio::ip::tcp::endpoint endpoint;
        endpoint.port(stateVariables_.openVpnPort);
        endpoint.address(boost::asio::ip::address_v4::from_string("127.0.0.1"));
        stateVariables_.socket.reset(new boost::asio::ip::tcp::socket(io_service_));
        stateVariables_.socket->async_connect(endpoint, boost::bind(&OpenVPNConnection::funcConnectToOpenVPN, this,
                                                                    boost::asio::placeholders::error));
    }
}

void OpenVPNConnection::handleRead(const boost::system::error_code &err, size_t bytes_transferred)
{
    Q_UNUSED(bytes_transferred);
    if (err.value() == 0)
    {
        std::istream is(stateVariables_.buffer.get());
        std::string resultLine;
        std::getline(is, resultLine);

        QString serverReply = QString::fromStdString(resultLine).trimmed();

        boost::system::error_code write_error;
        // skip log out BYTECOUNT
        if (!serverReply.contains(">BYTECOUNT:", Qt::CaseInsensitive))
        {
            qCDebug(LOG_OPENVPN) << serverReply;
        }
        if (serverReply.contains("HOLD:Waiting for hold release", Qt::CaseInsensitive))
        {
            boost::asio::write(*stateVariables_.socket, boost::asio::buffer("state on all\n"), boost::asio::transfer_all(), write_error);
        }
        else if (serverReply.startsWith("END") && stateVariables_.bWasStateNotification)
        {
            boost::asio::write(*stateVariables_.socket, boost::asio::buffer("log on\n"), boost::asio::transfer_all(), write_error);
        }
        else if (serverReply.contains("SUCCESS: real-time state notification set to ON", Qt::CaseInsensitive))
        {
            stateVariables_.bWasStateNotification = true;
            stateVariables_.isAcceptSigTermCommand_ = true;
        }
        else if (serverReply.contains("SUCCESS: real-time log notification set to ON", Qt::CaseInsensitive))
        {
            boost::asio::write(*stateVariables_.socket, boost::asio::buffer("bytecount 1\n"), boost::asio::transfer_all(), write_error);
        }
        else if (serverReply.contains("SUCCESS: bytecount interval changed", Qt::CaseInsensitive))
        {
            boost::asio::write(*stateVariables_.socket, boost::asio::buffer("hold release\n"), boost::asio::transfer_all(), write_error);
        }
        else if (serverReply.contains("PASSWORD:Need 'Auth' username/password", Qt::CaseInsensitive))
        {
            if (!username_.isEmpty())
            {
                char message[1024];
                sprintf(message, "username \"Auth\" %s\n", username_.toUtf8().data());
                boost::asio::write(*stateVariables_.socket, boost::asio::buffer(message,strlen(message)), boost::asio::transfer_all(), write_error);
            }
            else
            {
                emit requestUsername();
            }
        }
        else if (serverReply.contains("PASSWORD:Need 'HTTP Proxy' username/password", Qt::CaseInsensitive))
        {
            char message[1024];
            sprintf(message, "username \"HTTP Proxy\" %s\n", proxySettings_.getUsername().toUtf8().data());
            boost::asio::write(*stateVariables_.socket, boost::asio::buffer(message,strlen(message)), boost::asio::transfer_all(), write_error);
        }
        else if (serverReply.contains("'HTTP Proxy' username entered, but not yet verified", Qt::CaseInsensitive))
        {
            char message[1024];
            sprintf(message, "password \"HTTP Proxy\" %s\n", proxySettings_.getPassword().toUtf8().data());
            boost::asio::write(*stateVariables_.socket, boost::asio::buffer(message, strlen(message)), boost::asio::transfer_all(), write_error);
        }
        else if (serverReply.contains("'Auth' username entered, but not yet verified", Qt::CaseInsensitive))
        {
            if (!password_.isEmpty())
            {
                char message[1024];
                sprintf(message, "password \"Auth\" %s\n", password_.toUtf8().data());
                boost::asio::write(*stateVariables_.socket, boost::asio::buffer(message, strlen(message)), boost::asio::transfer_all(), write_error);
            }
            else
            {
                emit requestPassword();
            }
        }
        else if (serverReply.contains("PASSWORD:Verification Failed: 'Auth'", Qt::CaseInsensitive))
        {
            emit error(AUTH_ERROR);
            if (!stateVariables_.bSigTermSent)
            {
                boost::asio::write(*stateVariables_.socket, boost::asio::buffer("signal SIGTERM\n"), boost::asio::transfer_all(), write_error);
                stateVariables_.bSigTermSent = true;
            }
        }
        else if (serverReply.contains("There are no TAP-Windows adapters on this system", Qt::CaseInsensitive))
        {
            if (!stateVariables_.bTapErrorEmited)
            {
                emit error(NO_INSTALLED_TUN_TAP);
                stateVariables_.bTapErrorEmited = true;
                if (!stateVariables_.bSigTermSent)
                {
                    boost::asio::write(*stateVariables_.socket, boost::asio::buffer("signal SIGTERM\n"), boost::asio::transfer_all(), write_error);
                    stateVariables_.bSigTermSent = true;
                }
            }
        }
        else if (serverReply.startsWith(">BYTECOUNT:", Qt::CaseInsensitive))
        {
            QStringList pars = serverReply.split(":");
            if (pars.count() > 1)
            {
                QStringList pars2 = pars[1].split(",");
                if (pars2.count() == 2)
                {
                    quint64 l1 = pars2[0].toULongLong();
                    quint64 l2 = pars2[1].toULongLong();
                    if (stateVariables_.bFirstCalcStat)
                    {
                        stateVariables_.prevBytesRcved = l1;
                        stateVariables_.prevBytesXmited = l2;
                        emit statisticsUpdated(stateVariables_.prevBytesRcved, stateVariables_.prevBytesXmited, false);
                        stateVariables_.bFirstCalcStat = false;
                    }
                    else
                    {
                        emit statisticsUpdated(l1 - stateVariables_.prevBytesRcved, l2 - stateVariables_.prevBytesXmited, false);
                        stateVariables_.prevBytesRcved = l1;
                        stateVariables_.prevBytesXmited = l2;
                    }
                }
            }
        }
        else if (serverReply.startsWith(">STATE:", Qt::CaseInsensitive))
        {
            if (serverReply.contains("CONNECTED,SUCCESS", Qt::CaseInsensitive))
            {
                setCurrentState(STATUS_CONNECTED);
                emit connected();
            }
            else if (serverReply.contains("CONNECTED,ERROR", Qt::CaseInsensitive))
            {
                setCurrentState(STATUS_CONNECTED);
                emit error(CONNECTED_ERROR);
            }
            else if (serverReply.contains("RECONNECTING", Qt::CaseInsensitive))
            {
                stateVariables_.isAcceptSigTermCommand_ = false;
                stateVariables_.bWasStateNotification = false;
                setCurrentState(STATUS_CONNECTED_TO_SOCKET);
                emit reconnecting();
            }
        }
        else if (serverReply.startsWith(">LOG:", Qt::CaseInsensitive))
        {
            bool bContainsUDPWord = serverReply.contains("UDP", Qt::CaseInsensitive);
            if (bContainsUDPWord && serverReply.contains("No buffer space available (WSAENOBUFS) (code=10055)", Qt::CaseInsensitive))
            {
                emit error(UDP_CANT_ASSIGN);
            }
            else if (bContainsUDPWord && serverReply.contains("No Route to Host (WSAEHOSTUNREACH) (code=10065)", Qt::CaseInsensitive))
            {
                emit error(UDP_CANT_ASSIGN);
            }
            else if (bContainsUDPWord && serverReply.contains("Can't assign requested address (code=49)", Qt::CaseInsensitive))
            {
                emit error(UDP_CANT_ASSIGN);
            }
            else if (bContainsUDPWord && serverReply.contains("No buffer space available (code=55)", Qt::CaseInsensitive))
            {
                emit error(UDP_NO_BUFFER_SPACE);
            }
            else if (bContainsUDPWord && serverReply.contains("Network is down (code=50)", Qt::CaseInsensitive))
            {
                emit error(UDP_NETWORK_DOWN);
            }
            else if (serverReply.contains("TCP", Qt::CaseInsensitive) && serverReply.contains("failed", Qt::CaseInsensitive))
            {
                emit error(TCP_ERROR);
            }
            else if (serverReply.contains("Initialization Sequence Completed With Errors", Qt::CaseInsensitive))
            {
                emit error(INITIALIZATION_SEQUENCE_COMPLETED_WITH_ERRORS);
            }
            else if (serverReply.contains("TAP-WIN32 device", Qt::CaseInsensitive) && serverReply.contains("opened", Qt::CaseInsensitive))
            {
                int b = serverReply.indexOf("{");
                int e = serverReply.indexOf("}");
                if (b != -1 && e != -1 && b < e)
                {
                    QString tapName = serverReply.mid(b, e-b+1);
                    safeSetTapAdapter(tapName);
                }
                else
                {
                    safeSetTapAdapter("");
                    qCDebug(LOG_CONNECTION) << "Can't parse TAP name: " << serverReply;
                }
            }
        }
        else if (serverReply.contains(">FATAL:All TAP-Windows adapters on this system are currently in use", Qt::CaseInsensitive))
        {
            emit error(ALL_TAP_IN_USE);
        }

        checkErrorAndContinue(write_error, true);
    }
    else
    {
        qCDebug(LOG_CONNECTION) << "Read from openvpn socket connection failed, error:" << QString::fromStdString(err.message());
        setCurrentStateAndEmitDisconnected(STATUS_DISCONNECTED);
    }
}

void OpenVPNConnection::funcDisconnect()
{
    int curState = getCurrentState();
    if (!stateVariables_.bSigTermSent && (curState == STATUS_CONNECTED_TO_SOCKET || curState == STATUS_CONNECTED))
    {
        if (stateVariables_.isAcceptSigTermCommand_)
        {
            boost::system::error_code write_error;
            boost::asio::write(*stateVariables_.socket, boost::asio::buffer("signal SIGTERM\n"), boost::asio::transfer_all(), write_error);
            stateVariables_.bSigTermSent = true;
        }
        else
        {
            stateVariables_.bNeedSendSigTerm = true;
        }
    }
}

QString OpenVPNConnection::safeGetTapAdapter()
{
    QMutexLocker locker(&tapAdapterMutex_);
    return tapAdapter_;
}

void OpenVPNConnection::safeSetTapAdapter(const QString &tapAdapter)
{
    QMutexLocker locker(&tapAdapterMutex_);
    tapAdapter_ = tapAdapter;
}

void OpenVPNConnection::checkErrorAndContinue(boost::system::error_code &write_error, bool bWithAsyncReadCall)
{
    if (write_error.value() != 0)
    {
        qCDebug(LOG_CONNECTION) << "Write to openvpn socket connection failed, error:" << QString::fromStdString(write_error.message());
        setCurrentStateAndEmitDisconnected(STATUS_DISCONNECTED);
    }
    else
    {
        if (bWithAsyncReadCall)
        {
            boost::asio::async_read_until(*stateVariables_.socket, *stateVariables_.buffer, "\n",
                boost::bind(&OpenVPNConnection::handleRead, this,
                  boost::asio::placeholders::error, boost::asio::placeholders::bytes_transferred));
        }
    }

    if (stateVariables_.bNeedSendSigTerm && stateVariables_.isAcceptSigTermCommand_ && !stateVariables_.bSigTermSent)
    {
        boost::system::error_code new_write_error;
        boost::asio::write(*stateVariables_.socket, boost::asio::buffer("signal SIGTERM\n"), boost::asio::transfer_all(), new_write_error);
        stateVariables_.bSigTermSent = true;
    }
}

void OpenVPNConnection::continueWithUsernameImpl()
{
    boost::system::error_code write_error;
    char message[1024];
    sprintf(message, "username \"Auth\" %s\n", username_.toUtf8().data());
    boost::asio::write(*stateVariables_.socket, boost::asio::buffer(message,strlen(message)), boost::asio::transfer_all(), write_error);

    checkErrorAndContinue(write_error, false);
}

void OpenVPNConnection::continueWithPasswordImpl()
{
    boost::system::error_code write_error;
    char message[1024];
    sprintf(message, "password \"Auth\" %s\n", password_.toUtf8().data());
    boost::asio::write(*stateVariables_.socket, boost::asio::buffer(message, strlen(message)), boost::asio::transfer_all(), write_error);

    checkErrorAndContinue(write_error, false);
}
