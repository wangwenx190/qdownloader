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

#pragma once

#include "qdownloader_global.h"
#include <QElapsedTimer>
#include <QFile>
#include <QNetworkAccessManager>
#include <QObject>
#include <QUrl>

#define _WWX190_DL_DEFAULT_DOWNLOADING_POSTFIX "downloading"
#define _WWX190_DL_DEFAULT_DOWNLOADING_TIMEOUT 3000
#define _WWX190_DL_DEFAULT_DOWNLOADING_TRY_TIMES 5

class QDOWNLOADER_EXPORT QDownloader : public QObject
{
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(QDownloader)
    Q_PROPERTY(QUrl url READ url WRITE setUrl NOTIFY urlChanged)
    Q_PROPERTY(
        QString saveDirectory READ saveDirectory WRITE setSaveDirectory NOTIFY saveDirectoryChanged)
    Q_PROPERTY(qreal progress READ progress NOTIFY progressChanged)
    Q_PROPERTY(Speed speed READ speed NOTIFY speedChanged)
    Q_PROPERTY(int timeout READ timeout WRITE setTimeout NOTIFY timeoutChanged)
    Q_PROPERTY(FileInfo fileInfo READ fileInfo NOTIFY fileInfoChanged)
    Q_PROPERTY(QString downloadingPostfix READ downloadingPostfix WRITE setDownloadingPostfix NOTIFY
                   downloadingPostfixChanged)
    Q_PROPERTY(bool breakpointSupported READ breakpointSupported NOTIFY breakpointSupportedChanged)
    Q_PROPERTY(Proxy proxy READ proxy WRITE setProxy NOTIFY proxyChanged)

public:
    struct Speed
    {
        qreal value = 0.0;
        QString unit = {};
    };

    struct FileInfo
    {
        QString fileName = {};
        QString fileType = {};
        qint64 fileSize = 0;
    };

    enum class ProxyType { System, Socks5, Http };
    Q_ENUM(ProxyType)

    struct Proxy
    {
        ProxyType type = ProxyType::System;
        QString hostName = {};
        quint16 port = 0;
        QString userName = {};
        QString password = {};
    };

    explicit QDownloader(QObject *parent = nullptr);
    ~QDownloader() override;

    static FileInfo getRemoteFileInfo(const QUrl &val,
                                      int tryTimes = _WWX190_DL_DEFAULT_DOWNLOADING_TRY_TIMES,
                                      int tryTimeout = _WWX190_DL_DEFAULT_DOWNLOADING_TIMEOUT,
                                      bool *ok = nullptr);
    static QString uniqueFileName(
        const QString &value,
        const QString &dirPath,
        const QString &postfix = QString::fromUtf8(_WWX190_DL_DEFAULT_DOWNLOADING_POSTFIX));

public Q_SLOTS:
    void start();
    void pause();
    void resume();
    void stop();

    QUrl url() const;
    void setUrl(const QUrl &value);

    QString saveDirectory() const;
    void setSaveDirectory(const QString &value);

    int timeout() const;
    void setTimeout(int value = _WWX190_DL_DEFAULT_DOWNLOADING_TIMEOUT);

    qreal progress() const;

    Speed speed() const;

    FileInfo fileInfo() const;

    QString downloadingPostfix() const;
    void setDownloadingPostfix(
        const QString &val = QString::fromUtf8(_WWX190_DL_DEFAULT_DOWNLOADING_POSTFIX));

    bool breakpointSupported() const;

    Proxy proxy() const;
    void setProxy(Proxy val);

#if (QT_VERSION < QT_VERSION_CHECK(5, 15, 0))
protected:
    void timerEvent(QTimerEvent *event) override;
#endif

private Q_SLOTS:
    void onReadyRead();
    void onFinished();
    void onProgressChanged(qint64 bytesReceived, qint64 bytesTotal);

private:
    void start_internal();
    void resetData();
    void stopDownload();

Q_SIGNALS:
    void finished();
    void progressChanged();
    void speedChanged();
    void fileInfoChanged();
    void urlChanged();
    void timeoutChanged();
    void saveDirectoryChanged();
    void downloadingPostfixChanged();
    void breakpointSupportedChanged();
    void proxyChanged();

private:
    QUrl m_url = {};
    QFile m_file = {};
    QNetworkAccessManager m_manager;
    QNetworkReply *m_reply = nullptr;
    QElapsedTimer m_speedTimer = {};
    QString m_saveDirectory = {},
            m_downloadingPostfix = QString::fromUtf8(_WWX190_DL_DEFAULT_DOWNLOADING_POSTFIX);
    qreal m_progress = 0.0;
    int m_timeout = _WWX190_DL_DEFAULT_DOWNLOADING_TIMEOUT, m_timeoutTimerId = 0;
    Speed m_speed = {};
    bool m_downloading = false, m_paused = false;
    qint64 m_receivedBytes = 0, m_totalBytes = 0, m_currentReceivedBytes = 0,
           m_bytesreceived_timer = 0;
    FileInfo m_fileInfo = {};
};

Q_DECLARE_METATYPE(QDownloader::Speed)
Q_DECLARE_METATYPE(QDownloader::FileInfo)
Q_DECLARE_METATYPE(QDownloader::Proxy)
