/*
*  Copyright (C) 2010 Felix Geyer <debfx@fobos.de>
*
*  This program is free software: you can redistribute it and/or modify
*  it under the terms of the GNU General Public License as published by
*  the Free Software Foundation, either version 2 or (at your option)
*  version 3 of the License.
*
*  This program is distributed in the hope that it will be useful,
*  but WITHOUT ANY WARRANTY; without even the implied warranty of
*  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
*  GNU General Public License for more details.
*
*  You should have received a copy of the GNU General Public License
*  along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "HashedBlockStream.h"

#include <cstring>

#include "core/Endian.h"
#include "crypto/CryptoHash.h"

const QSysInfo::Endian HashedBlockStream::BYTEORDER = QSysInfo::LittleEndian;

HashedBlockStream::HashedBlockStream(QIODevice* baseDevice)
    : LayeredStream(baseDevice)
    , m_blockSize(1024*1024)
{
    init();
}

HashedBlockStream::HashedBlockStream(QIODevice* baseDevice, qint32 blockSize)
    : LayeredStream(baseDevice)
    , m_blockSize(blockSize)
{
    init();
}

void HashedBlockStream::init()
{
    m_bufferPos = 0;
    m_blockIndex = 0;
    m_eof = false;
    m_error = false;
}

bool HashedBlockStream::reset()
{
    if (isWritable()) {
        if (!m_buffer.isEmpty()) {
            if (!writeHashedBlock()) {
                return false;
            }
        }

        // write empty final block
        if (!writeHashedBlock()) {
            return false;
        }
    }

    init();
    m_buffer.clear();

    return true;
}

void HashedBlockStream::close()
{
    if (isWritable()) {
        if (!m_buffer.isEmpty()) {
            writeHashedBlock();
        }

        // write empty final block
        writeHashedBlock();
    }

    LayeredStream::close();
}

qint64 HashedBlockStream::readData(char* data, qint64 maxSize)
{
    if (m_eof) {
        return 0;
    }

    qint64 bytesRemaining = maxSize;
    qint64 offset = 0;

    while (bytesRemaining > 0) {
        if (m_bufferPos == m_buffer.size()) {
            if (!readHashedBlock()) {
                return maxSize - bytesRemaining;
            }
        }

        int bytesToCopy = qMin(bytesRemaining, static_cast<qint64>(m_buffer.size() - m_bufferPos));

        memcpy(data + offset, m_buffer.constData() + m_bufferPos, bytesToCopy);

        offset += bytesToCopy;
        m_bufferPos += bytesToCopy;
        bytesRemaining -= bytesToCopy;
    }

    return maxSize;
}

bool HashedBlockStream::readHashedBlock()
{
    bool ok;

    quint32 index = Endian::readUInt32(m_baseDevice, BYTEORDER, &ok);
    if (!ok || index != m_blockIndex) {
        // TODO error
        Q_ASSERT(false);
        return false;
    }

    QByteArray hash = m_baseDevice->read(32);
    if (hash.size() != 32) {
        // TODO error
        Q_ASSERT(false);
        return false;
    }

    m_blockSize = Endian::readInt32(m_baseDevice, BYTEORDER, &ok);
    if (!ok || m_blockSize < 0) {
        // TODO error
        Q_ASSERT(false);
        return false;
    }

    if (m_blockSize == 0) {
        if (hash.count(static_cast<char>(0)) != 32) {
            // TODO error
            Q_ASSERT(false);
            return false;
        }

        m_eof = true;
        return false;
    }

    m_buffer = m_baseDevice->read(m_blockSize);
    if (m_buffer.size() != m_blockSize) {
        // TODO error
        Q_ASSERT(false);
        return false;
    }

    if (hash != CryptoHash::hash(m_buffer, CryptoHash::Sha256)) {
        // TODO error
        Q_ASSERT(false);
        return false;
    }

    m_bufferPos = 0;
    m_blockIndex++;

    return true;
}

qint64 HashedBlockStream::writeData(const char* data, qint64 maxSize)
{
    Q_ASSERT(maxSize >= 0);

    if (m_error) {
        return 0;
    }

    qint64 bytesRemaining = maxSize;
    qint64 offset = 0;

    while (bytesRemaining > 0) {
        int bytesToCopy = qMin(bytesRemaining, static_cast<qint64>(m_blockSize - m_buffer.size()));

        m_buffer.append(data + offset, bytesToCopy);

        offset += bytesToCopy;
        bytesRemaining -= bytesToCopy;

        if (m_buffer.size() == m_blockSize) {
            if (!writeHashedBlock()) {
                if (m_error) {
                    return -1;
                }
                else {
                    return maxSize - bytesRemaining;
                }
            }
        }
    }

    return maxSize;
}

bool HashedBlockStream::writeHashedBlock()
{
    if (!Endian::writeInt32(m_blockIndex, m_baseDevice, BYTEORDER)) {
        // TODO error
        Q_ASSERT(false);
        return false;
    }
    m_blockIndex++;

    QByteArray hash;
    if (!m_buffer.isEmpty()) {
        hash = CryptoHash::hash(m_buffer, CryptoHash::Sha256);
    }
    else {
        hash.fill(0, 32);
    }

    if (m_baseDevice->write(hash) != hash.size()) {
        // TODO error
        Q_ASSERT(false);
        return false;
    }

    if (!Endian::writeInt32(m_buffer.size(), m_baseDevice, BYTEORDER)) {
        // TODO error
        Q_ASSERT(false);
        return false;
    }

    if (!m_buffer.isEmpty()) {
        if (m_baseDevice->write(m_buffer) != m_buffer.size()) {
            // TODO error
            Q_ASSERT(false);
            return false;
        }

        m_buffer.clear();
    }

    return true;
}
