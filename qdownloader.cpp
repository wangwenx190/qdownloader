/*
 * MIT License
 *
 * Copyright (C) 2020 by wangwenx190 (Yuhang Zhao)
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include "qdownloader.h"

#include <QCoreApplication>
#include <QDeadlineTimer>
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QNetworkProxy>
#include <QNetworkReply>
#include <QNetworkRequest>
#if (QT_VERSION < QT_VERSION_CHECK(5, 15, 0))
#include <QTimerEvent>
#endif

QDownloader::QDownloader(QObject *parent) : QObject(parent)
{
    qRegisterMetaType<Speed>();
    qRegisterMetaType<FileInfo>();
    qRegisterMetaType<Proxy>();
    m_saveDirectory = QDir::toNativeSeparators(QCoreApplication::applicationDirPath());
    QNetworkProxyFactory::setUseSystemConfiguration(true);
#if (QT_VERSION >= QT_VERSION_CHECK(5, 9, 0))
    // Allow url redirection.
    m_manager.setRedirectPolicy(QNetworkRequest::NoLessSafeRedirectPolicy);
#endif
#if (QT_VERSION >= QT_VERSION_CHECK(5, 14, 0))
    m_manager.setAutoDeleteReplies(true);
#endif
    connect(&m_manager, &QNetworkAccessManager::finished, this, &QDownloader::onFinished);
}

QDownloader::~QDownloader()
{
    m_manager.disconnect();
    stop();
}

QString QDownloader::uniqueFileName(const QString &value,
                                    const QString &dirPath,
                                    const QString &postfix)
{
    if (value.isEmpty() || dirPath.isEmpty() || postfix.isEmpty()) {
        return {};
    }
    const QFileInfo fileInfo(value);
    // FIXME: example.tar.gz -> example.tar (1).gz
    // Not good! Should be example (1).tar.gz
    const QString suffix = fileInfo.suffix();
    QString baseName = fileInfo.completeBaseName();
    if (QFile::exists(QString::fromUtf8("%1/%2").arg(dirPath, value))) {
        int i = 1;
        baseName += QString::fromUtf8(" (");
        while (QFile::exists(
            QString::fromUtf8("%1/%2%3).%4").arg(dirPath, baseName, QString::number(i), suffix))) {
            ++i;
        }
        baseName += QString::number(i) + QChar::fromLatin1(')');
    }
    return QString::fromUtf8("%1.%2.%3").arg(baseName, suffix, postfix);
}

void QDownloader::start_internal()
{
    if (m_downloading) {
        return;
    }
    if (m_file.isOpen()) {
        m_file.close();
    }
    const bool append = breakpointSupported() && (m_currentReceivedBytes > 0);
    if (!append) {
        m_file.setFileName(QString::fromUtf8("%1/%2").arg(m_saveDirectory,
                                                          uniqueFileName(m_fileInfo.fileName,
                                                                         m_saveDirectory,
                                                                         m_downloadingPostfix)));
    }
    if ((m_currentReceivedBytes <= 0) && m_file.exists()) {
        m_file.remove();
    }
    if (!m_file.open(QFile::WriteOnly | (append ? QFile::Append : QFile::Truncate))) {
        qDebug() << "Cannot open file for writing.";
        return;
    }
    m_downloading = true;
    m_paused = false;
    m_speedTimer.start();
#if (QT_VERSION < QT_VERSION_CHECK(5, 15, 0))
    m_timeoutTimerId = startTimer(m_timeout);
#endif
    QNetworkRequest request(m_url);
#if (QT_VERSION < QT_VERSION_CHECK(5, 9, 0))
    request.setAttribute(QNetworkRequest::FollowRedirectsAttribute, true);
#else
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
#endif
    if (append) {
        const QString headerContent = QString::fromUtf8("bytes=%1-").arg(m_currentReceivedBytes);
        request.setRawHeader("Range", headerContent.toUtf8());
    }
    m_reply = m_manager.get(request);
    connect(m_reply, &QNetworkReply::downloadProgress, this, &QDownloader::onProgressChanged);
    connect(m_reply, &QNetworkReply::readyRead, this, &QDownloader::onReadyRead);
}

QUrl QDownloader::url() const
{
    return m_url;
}

void QDownloader::setUrl(const QUrl &value)
{
    if (!value.isValid()) {
        qDebug() << "The given URL is not valid:" << value;
        return;
    }
    if (m_url != value) {
        m_url = value;
        Q_EMIT urlChanged();
    }
}

QString QDownloader::saveDirectory() const
{
    return m_saveDirectory;
}

void QDownloader::setSaveDirectory(const QString &value)
{
    if (value.isEmpty()) {
        qDebug() << "The given path is empty.";
        return;
    }
    if (m_saveDirectory != value) {
        m_saveDirectory = QDir::toNativeSeparators(value);
        Q_EMIT saveDirectoryChanged();
        const QDir dir(value);
        // Create the directory if it doesn't exist.
        if (!dir.exists()) {
            dir.mkpath(QChar::fromLatin1('.'));
        }
    }
}

int QDownloader::timeout() const
{
    return qMax(m_timeout, 0);
}

void QDownloader::setTimeout(int value)
{
    if (value < 0) {
        qDebug() << "The minimum of timeout is zero.";
        return;
    }
    if (m_timeout != value) {
        m_timeout = value;
#if (QT_VERSION >= QT_VERSION_CHECK(5, 15, 0))
        m_manager.setTransferTimeout(m_timeout);
#endif
        Q_EMIT timeoutChanged();
    }
}

qreal QDownloader::progress() const
{
    return qBound(0.0, m_progress, 1.0);
}

QDownloader::Speed QDownloader::speed() const
{
    return m_speed;
}

void QDownloader::resetData()
{
    m_url.clear();
    m_progress = 0.0;
    m_speed.value = 0.0;
    m_speed.unit.clear();
    m_downloading = false;
    m_receivedBytes = 0;
    m_totalBytes = 0;
    m_currentReceivedBytes = 0;
    m_fileInfo.fileName.clear();
    m_fileInfo.fileType.clear();
    m_fileInfo.fileSize = 0;
    m_paused = false;
    m_bytesreceived_timer = 0;
    m_timeoutTimerId = 0;
}

void QDownloader::onReadyRead()
{
    if (!m_downloading) {
        return;
    }
    if (!m_file.isOpen()) {
        // FIXME: Stop and exit or re-open it and continue?
        qDebug() << "Internal error: file is not open for writing. Aborting...";
        stop();
        return;
    }
    // The following code is copied from Qt Installer Framework.
    QByteArray buffer(32768, Qt::Uninitialized);
    while (m_reply->bytesAvailable()) {
        const qint64 read = m_reply->read(buffer.data(), buffer.size());
        qint64 written = 0;
        while (written < read) {
            const qint64 toWrite = m_file.write(buffer.constData() + written, read - written);
            if (toWrite < 0) {
                qDebug() << QString::fromUtf8(R"(Writing to file "%1" failed: %2)")
                                .arg(QDir::toNativeSeparators(m_file.fileName()),
                                     m_file.errorString());
                return;
            }
            written += toWrite;
        }
    }
}

void QDownloader::onFinished()
{
    m_downloading = false;
#if (QT_VERSION < QT_VERSION_CHECK(5, 15, 0))
    killTimer(m_timeoutTimerId);
#endif
    m_reply->disconnect();
    m_reply->close();
    if (m_file.isOpen()) {
        m_file.close();
    }
    if (m_paused) {
        return;
    }
    // Handle url redirection.
    if (m_reply->attribute(QNetworkRequest::RedirectionTargetAttribute).isValid()) {
        m_file.remove();
        m_url = m_reply->url().resolved(
            m_reply->attribute(QNetworkRequest::RedirectionTargetAttribute).toUrl());
        // Update file information from the redirected url, but don't change the
        // file name as the new file name returned by the server may be invalid.
        const FileInfo _fi = getRemoteFileInfo(m_url);
        m_fileInfo.fileType = _fi.fileType;
        m_fileInfo.fileSize = _fi.fileSize;
        Q_EMIT fileInfoChanged();
        m_reply->deleteLater();
        m_reply = nullptr;
        Q_EMIT urlChanged();
        // Download the real file.
        start_internal();
        return;
    }
    if (m_reply->error() == QNetworkReply::NoError) {
        // Remove the temporary file extension name.
        if (!m_file.rename(QString::fromUtf8("%1/%2").arg(m_saveDirectory,
                                                          QFileInfo(m_file).completeBaseName()))) {
            qDebug() << "Failed to rename the downloaded file. Check your "
                        "anti-virous software.";
        }
    } else {
        m_file.remove();
        qDebug() << "Download failed:" << m_reply->errorString();
    }
    m_reply->deleteLater();
    m_reply = nullptr;
    resetData();
    Q_EMIT finished();
}

void QDownloader::onProgressChanged(qint64 bytesReceived, qint64 bytesTotal)
{
    if (!m_downloading) {
        return;
    }
#if (QT_VERSION < QT_VERSION_CHECK(5, 15, 0))
    m_bytesreceived_timer = m_receivedBytes;
#endif
    m_receivedBytes = bytesReceived;
    m_totalBytes = bytesTotal;
    m_progress = qreal(bytesReceived + m_currentReceivedBytes)
                 / qreal(bytesTotal + m_currentReceivedBytes);
    m_speed.value = qreal(bytesReceived) * 1000.0 / qreal(m_speedTimer.elapsed());
    if (m_speed.value < 1024.0) {
        m_speed.unit = QString::fromUtf8("B/s");
    } else if (m_speed.value < 1024.0 * 1024.0) {
        m_speed.value /= 1024.0;
        m_speed.unit = QString::fromUtf8("KB/s");
    } else {
        m_speed.value /= 1024.0 * 1024.0;
        m_speed.unit = QString::fromUtf8("MB/s");
    }
    Q_EMIT progressChanged();
    Q_EMIT speedChanged();
}

void QDownloader::stop()
{
    stopDownload();
    // Remove un-finished file.
    if ((QFileInfo(m_file).suffix() == m_downloadingPostfix) && m_file.exists()) {
        m_file.remove();
    }
    resetData();
}

void QDownloader::start()
{
    if (m_paused) {
        qDebug() << "Use the \"Downloader::resume()\" method to re-start a "
                    "paused download.";
        return;
    }
    if (m_downloading) {
        qDebug() << "Stop the current download task first before start a new one.";
        return;
    }
    if (!m_url.isValid() || m_saveDirectory.isEmpty()) {
        qDebug() << "The URL is not valid and/or the save directory is not set.";
        return;
    }
    m_fileInfo = getRemoteFileInfo(m_url);
    Q_EMIT fileInfoChanged();
    Q_EMIT breakpointSupportedChanged();
    // FIXME: Start too quickly causes problems?
    // QThread::usleep(50);
    start_internal();
}

QDownloader::FileInfo QDownloader::fileInfo() const
{
    return m_fileInfo;
}

void QDownloader::pause()
{
    if (!m_downloading || m_paused) {
        qDebug() << "Download already paused or stopped.";
        return;
    }
    if (!breakpointSupported()) {
        qDebug() << "Current download task doesn't support breakpoint transfer.";
        qDebug() << "Downloading stopped.";
        stop();
        return;
    }
    m_paused = true;
    stopDownload();
    m_currentReceivedBytes += m_receivedBytes;
}

void QDownloader::stopDownload()
{
#if (QT_VERSION < QT_VERSION_CHECK(5, 15, 0))
    killTimer(m_timeoutTimerId);
#endif
    if (m_reply) {
        m_reply->disconnect();
        if (m_reply->isRunning()) {
            m_reply->abort();
        }
        m_reply->deleteLater();
        m_reply = nullptr;
    }
    if (m_file.isOpen()) {
        m_file.close();
    }
    m_downloading = false;
}

void QDownloader::resume()
{
    if (m_downloading || !m_paused) {
        qDebug() << "Download already running.";
        return;
    }
    m_paused = false;
    start_internal();
}

QString QDownloader::downloadingPostfix() const
{
    return m_downloadingPostfix;
}

void QDownloader::setDownloadingPostfix(const QString &val)
{
    if (val.isEmpty()) {
        return;
    }
    if (m_downloadingPostfix != val) {
        if (m_downloadingPostfix.startsWith(QChar::fromLatin1('.'))) {
            m_downloadingPostfix = val.mid(1);
        } else {
            m_downloadingPostfix = val;
        }
        Q_EMIT downloadingPostfixChanged();
    }
}

QDownloader::FileInfo QDownloader::getRemoteFileInfo(const QUrl &val,
                                                     int tryTimes,
                                                     int tryTimeout,
                                                     bool *ok)
{
    if (!val.isValid()) {
        qDebug() << "Invalid URL:" << val;
        if (ok) {
            *ok = false;
        }
        return FileInfo{};
    }
    if (tryTimes < 1) {
        qDebug() << "The minimum try times cannot be lower than one.";
        if (ok) {
            *ok = false;
        }
        return FileInfo{};
    }
    if (tryTimeout < 1000) {
        qDebug() << "The minimum try timeout cannot be lower than one thousand.";
        if (ok) {
            *ok = false;
        }
        return FileInfo{};
    }
    bool ready = false;
    FileInfo headFileInfo;
    for (int i = 0; i != tryTimes; ++i) {
        QDeadlineTimer deadline(tryTimeout);
        QNetworkAccessManager headManager;
#if (QT_VERSION >= QT_VERSION_CHECK(5, 15, 0))
        headManager.setTransferTimeout(tryTimeout);
#endif
#if 0
#if (QT_VERSION >= QT_VERSION_CHECK(5, 9, 0))
    headManager.setRedirectPolicy(QNetworkRequest::NoLessSafeRedirectPolicy);
#endif
#endif
#if (QT_VERSION >= QT_VERSION_CHECK(5, 14, 0))
        headManager.setAutoDeleteReplies(true);
#endif
        QNetworkRequest headRequest(val);
#if 0
#if (QT_VERSION < QT_VERSION_CHECK(5, 9, 0))
        headRequest.setAttribute(QNetworkRequest::FollowRedirectsAttribute, true);
#else
        headRequest.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
#endif
#endif
        QNetworkReply *headReply = headManager.head(headRequest);
        connect(headReply, &QNetworkReply::finished, [headReply, val, ok, &headFileInfo, &ready]() {
            if (headReply->error() == QNetworkReply::NoError) {
                headFileInfo.fileType = headReply->header(QNetworkRequest::ContentTypeHeader)
                                            .toString();
                headFileInfo.fileSize = headReply->header(QNetworkRequest::ContentLengthHeader)
                                            .toLongLong();
                if (headFileInfo.fileSize <= 0) {
                    headFileInfo.fileSize = 0;
                    qDebug() << "Failed to query file size from server.";
                }
                const QString disposition
                    = headReply->header(QNetworkRequest::ContentDispositionHeader).toString();
                const int index = disposition.indexOf(QString::fromUtf8("filename="),
                                                      Qt::CaseInsensitive);
                const QString fileName = disposition.mid(index + 9);
                if (fileName.isEmpty()) {
                    qDebug() << "Failed to query file name from server. Using "
                                "the default file name parsed from the URL "
                                "instead.";
                    headFileInfo.fileName = val.fileName();
                } else {
                    headFileInfo.fileName = fileName;
                }
                if (ok) {
                    *ok = true;
                }
            } else {
                qDebug() << "Failed to query file information from server:"
                         << headReply->errorString();
                headFileInfo.fileName = val.fileName();
                if (ok) {
                    *ok = false;
                }
            }
            headReply->disconnect();
            headReply->deleteLater();
            // headReply = nullptr;
            ready = true;
        });
        while (!deadline.hasExpired() && !ready) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        }
        if (ready) {
            break;
        }
    }
    return headFileInfo;
}

#if (QT_VERSION < QT_VERSION_CHECK(5, 15, 0))
void Downloader::timerEvent(QTimerEvent *event)
{
    if (event->timerId() == m_timeoutTimerId) {
        if (m_receivedBytes <= m_bytesreceived_timer) {
            qDebug() << "Error: network transfer timeout.";
            stop();
        }
    }
    QObject::timerEvent(event);
}
#endif

bool QDownloader::breakpointSupported() const
{
    // FIXME: Is there an official way to judge this?
    return m_url.scheme().startsWith(QString::fromUtf8("http"), Qt::CaseInsensitive);
}

QDownloader::Proxy QDownloader::proxy() const
{
    if (QNetworkProxyFactory::usesSystemConfiguration()) {
        return {ProxyType::System, {}, 0, {}, {}};
    }
    const QNetworkProxy _p = m_manager.proxy();
    Proxy _p2;
    _p2.hostName = _p.hostName();
    _p2.port = _p.port();
    _p2.userName = _p.user();
    _p2.password = _p.password();
    if (_p.type() == QNetworkProxy::ProxyType::Socks5Proxy) {
        _p2.type = ProxyType::Socks5;
    } else if (_p.type() == QNetworkProxy::ProxyType::HttpProxy) {
        _p2.type = ProxyType::Http;
    }
    return _p2;
}

void QDownloader::setProxy(Proxy val)
{
    if (val.type == ProxyType::System) {
        QNetworkProxyFactory::setUseSystemConfiguration(true);
    } else {
        QNetworkProxyFactory::setUseSystemConfiguration(false);
        QNetworkProxy _p;
        _p.setHostName(val.hostName);
        _p.setPort(val.port);
        _p.setUser(val.userName);
        _p.setPassword(val.password);
        if (val.type == ProxyType::Socks5) {
            _p.setType(QNetworkProxy::ProxyType::Socks5Proxy);
        } else if (val.type == ProxyType::Http) {
            _p.setType(QNetworkProxy::ProxyType::HttpProxy);
        }
        m_manager.setProxy(_p);
    }
    Q_EMIT proxyChanged();
}
