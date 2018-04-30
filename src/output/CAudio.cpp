/*
 *    Copyright (C) 2018
 *    Matthias P. Braendli (matthias.braendli@mpb.li)
 *
 *    Copyright (C) 2017
 *    Albrecht Lohofener (albrechtloh@gmx.de)
 *
 *    This file is part of the welle.io.
 *    Many of the ideas as implemented in welle.io are derived from
 *    other work, made available through the GNU general Public License. 
 *    All copyrights of the original authors are recognized.
 *
 *    welle.io is free software; you can redistribute it and/or modify
 *    it under the terms of the GNU General Public License as published by
 *    the Free Software Foundation; either version 2 of the License, or
 *    (at your option) any later version.
 *
 *    welle.io is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU General Public License for more details.
 *
 *    You should have received a copy of the GNU General Public License
 *    along with welle.io; if not, write to the Free Software
 *    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

#include <QDebug>
#include <stdio.h>

#include "CAudio.h"

CAudioThread::CAudioThread(RingBuffer<int16_t>& buffer, QObject *parent) :
    QThread(parent),
    buffer(buffer),
    audioIODevice(buffer, this)
{
    audioOutput = NULL;
    CardRate = 48000;

    connect(&CheckAudioBufferTimer, &QTimer::timeout,
            this, &CAudioThread::checkAudioBufferTimeout);

    // Check audio state every 1 s, start audio if bytes are available
    CheckAudioBufferTimer.start(1000);

    // Move event processing of CAudioThread to this thread
    QObject::moveToThread(this);
}

CAudioThread::~CAudioThread(void)
{
    qDebug() << "Destructor of CAudioThread: " << QThread::currentThreadId();
}

void CAudioThread::setRate(int sampleRate)
{
    if (CardRate != sampleRate) {
        qDebug() << "Audio:"
                 << "Sample rate" << sampleRate << "Hz";
        CardRate = sampleRate;
        // restart audio within thread with new sample rate
        qDebug() << "Restart Audio with rate " << CardRate << " from Thread: " << QThread::currentThreadId();
        init(CardRate);
    }
}

void CAudioThread::setVolume(qreal volume)
{
    if (audioOutput != NULL) {
        qDebug() << "Audio:"
                 << "Volume" << volume;
        audioOutput->setVolume(volume);
    }
}

void CAudioThread::init(int sampleRate)
{
    if (audioOutput != NULL) {
        delete audioOutput;
        audioOutput = NULL;
    }

    AudioFormat.setSampleRate(sampleRate);
    AudioFormat.setChannelCount(2);
    AudioFormat.setSampleSize(16);
    AudioFormat.setCodec("audio/pcm");
    AudioFormat.setByteOrder(QAudioFormat::LittleEndian);
    AudioFormat.setSampleType(QAudioFormat::SignedInt);

    QAudioDeviceInfo info(QAudioDeviceInfo::defaultOutputDevice());
    if (!info.isFormatSupported(AudioFormat)) {
        qDebug() << "Audio:"
                 << "Audio format \"audio/pcm\" 16-bit stereo not supported. Your audio may not work!";
    }

    audioOutput = new QAudioOutput(AudioFormat, this);
    audioOutput->setBufferSize(audioOutput->bufferSize()*2);
    connect(audioOutput, &QAudioOutput::stateChanged, this, &CAudioThread::handleStateChanged);

    audioIODevice.start();
    audioOutput->start(&audioIODevice);
}

void CAudioThread::run()
{
    qDebug() << "Start Audio with rate " << CardRate << " from Thread: " << QThread::currentThreadId();
    // QAudioOutput needs to create within run()
    init(CardRate);
    // start event loop of QThread
    exec();
    qDebug() << "End of event loop from Thread: " << QThread::currentThreadId();
}

void CAudioThread::stop(void)
{
    audioIODevice.stop();
    audioOutput->stop();
}

void CAudioThread::reset(void)
{
    audioIODevice.flush();
    audioOutput->reset();
}

void CAudioThread::handleStateChanged(QAudio::State newState)
{
    CurrentState = newState;

    switch (newState) {
    case QAudio::ActiveState:
        qDebug() << "Audio:"
                 << "ActiveState";
        break;
    case QAudio::SuspendedState:
        qDebug() << "Audio:"
                 << "SuspendedState";
        break;
    case QAudio::StoppedState:
        qDebug() << "Audio:"
                 << "StoppedState";
        break;
    case QAudio::IdleState:
        qDebug() << "Audio:"
                 << "IdleState";
        // Necessary to avoid a IdleState, ActiveState, IdleState, ActiveState ... loop under Ubuntu. I don't know why.
        audioOutput->stop();
        break;
    default:
        qDebug() << "Audio:"
                 << "Unknown state:" << newState;
        break;
    }
}

void CAudioThread::checkAudioBufferTimeout()
{
    int32_t Bytes = buffer.GetRingBufferReadAvailable();

    // Start audio if bytes are available and audio is not active
    if (audioOutput && Bytes && CurrentState != QAudio::ActiveState) {
        audioIODevice.start();
        audioOutput->start(&audioIODevice);
    }
}

CAudioIODevice::CAudioIODevice(RingBuffer<int16_t>& buffer, QObject* parent) :
    QIODevice(parent),
    buffer(buffer)
{
}

CAudioIODevice::~CAudioIODevice()
{
}

void CAudioIODevice::start()
{
    open(QIODevice::ReadOnly);
}

void CAudioIODevice::stop()
{
    buffer.FlushRingBuffer();
    close();
}

void CAudioIODevice::flush()
{
    buffer.FlushRingBuffer();
}

qint64 CAudioIODevice::readData(char* data, qint64 len)
{
    qint64 total = 0;

    total = buffer.getDataFromBuffer(data, len / 2); // we have int16 samples

    // If the buffer is empty return zeros.
    if(total == 0)
    {
        memset(data, 0, len);
        total = len / 2;
    }

    return total * 2;
}

qint64 CAudioIODevice::writeData(const char* data, qint64 len)
{
    Q_UNUSED(data);
    Q_UNUSED(len);

    return 0;
}

qint64 CAudioIODevice::bytesAvailable() const
{
    return buffer.GetRingBufferReadAvailable();
}

CAudio::CAudio(RingBuffer<int16_t>& buffer, QObject *parent) :
    QObject(parent),
    buffer(buffer),
    audioIODevice(buffer, this),
    _audioThread(NULL)
{
    qDebug() << "Create CAudioThread from main thread: " << QThread::currentThreadId();
    _audioThread = new CAudioThread(buffer);
    _audioThread->start();
}
     
CAudio::~CAudio(void)
{
    qDebug() << "Destructor of CAudio from main thread: " << QThread::currentThreadId();
    if (_audioThread != NULL)
    {
        qDebug() << "Destructor of CAudio from main thread stops CAudioThread: " << QThread::currentThreadId();
        _audioThread->quit();
        _audioThread->wait();
    }

}

void CAudio::stop(void)
{
    // Call stopInternal of CAudioThread (and invoke it in the other thread)
    QMetaObject::invokeMethod(_audioThread, "stop", Qt::QueuedConnection);
}

void CAudio::reset(void)
{
    // Call resetInternal of CAudioThread (and invoke it in the other thread)
    QMetaObject::invokeMethod(_audioThread, "reset", Qt::QueuedConnection);
}

void CAudio::setRate(int sampleRate)
{
    // Call setRateInternal of CAudioThread (and invoke it in the other thread)
    QMetaObject::invokeMethod(_audioThread, "setRate", Qt::QueuedConnection, Q_ARG(int, sampleRate));
}

void CAudio::setVolume(qreal volume)
{
    // Call setVolumeInternal of CAudioThread (and invoke it in the other thread)
    QMetaObject::invokeMethod(_audioThread, "setVolume", Qt::QueuedConnection, Q_ARG(qreal, volume));
}


