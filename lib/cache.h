/*
  This file is part of Allie Chess.
  Copyright (C) 2018, 2019 Adam Treat

  Allie Chess is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  Allie Chess is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with Allie Chess.  If not, see <http://www.gnu.org/licenses/>.

  Additional permission under GNU GPL version 3 section 7
*/

#ifndef CACHE_H
#define CACHE_H

#include <QtGlobal>
#include <QtMath>
#include <QHash>
#include <unordered_map>

#include "node.h"
#include "options.h"

//#define DEBUG_CACHE
//#define DEBUG_SANITY

template <class T>
extern quint64 fixedHash(const T &object);

template <class T>
class FixedSizeCache {
public:
    FixedSizeCache();
    ~FixedSizeCache();

    void reset(int positions);
    bool contains(quint64 hash) const;
    T *object(quint64 hash, bool relink);
    T *newObject(quint64 hash);
    void unlink(quint64 hash);
    void pin(quint64 hash);
    void unpin(quint64 hash);
    void clearPinned();
    float percentFull(int halfMoveNumber) const;
    int size() const { return m_size; }
    int used() const { return m_used; }

private:
    struct ObjectInfo {
        inline ObjectInfo() :
            previous(nullptr),
            next(nullptr),
            pinned(false)
        {}
        ObjectInfo *previous;
        ObjectInfo *next;
        T object;
        bool pinned: 1;
    };

    void clear();
    void sanityCheck();
    ObjectInfo* unlinkFromUsed();
    ObjectInfo* unlinkFromUnused();
    void linkToUsed(ObjectInfo &);
    void relinkToUsed(ObjectInfo &);
    void relinkToUnused(ObjectInfo &);
    ObjectInfo *m_first;
    ObjectInfo *m_last;
    ObjectInfo *m_unused;
    std::unordered_map<quint64, ObjectInfo*> m_cache;
    int m_size;
    int m_used;
};

class Cache {
public:
    static Cache *globalInstance();

    void reset();
    float percentFull(int halfMoveNumber) const;
    int size() const;
    int used() const;

    bool containsNode(quint64 hash) const;
    Node *node(quint64 hash, bool relink = false);
    Node *newNode(quint64 hash);
    void unlinkNode(quint64 hash);
    void pinNode(quint64 hash);
    void unpinNode(quint64 hash);

    bool containsNodePosition(quint64 hash) const;
    Node::Position *nodePosition(quint64 hash, bool relink = false);
    Node::Position *newNodePosition(quint64 hash);
    void unlinkNodePosition(quint64 hash);
    void pinNodePosition(quint64 hash);
    void unpinNodePosition(quint64 hash);

private:
    friend class MyCache;
    FixedSizeCache<Node> m_nodeCache;
    FixedSizeCache<Node::Position> m_positionCache;
};

template <class T>
inline FixedSizeCache<T>::FixedSizeCache()
    : m_first(nullptr),
    m_last(nullptr),
    m_unused(nullptr),
    m_size(0),
    m_used(0)
{
}

template <class T>
inline FixedSizeCache<T>::~FixedSizeCache()
{
    clear();
}

template <class T>
inline void FixedSizeCache<T>::reset(int positions)
{
    clear();
    if (!positions)
        return;

    m_size = positions;
    for (int i = 0; i < m_size; ++i) {
        ObjectInfo *info = new ObjectInfo;
        if (m_unused) {
            info->next = m_unused;
            m_unused->previous = info;
        }
        m_unused = info;
        m_unused->previous = nullptr;
    }

    // FIXME: Still see malloc during insertion
    m_cache.reserve(m_size);
    Q_ASSERT(!m_first);
    Q_ASSERT(!m_last);
    Q_ASSERT(m_unused);

#if defined(DEBUG_CACHE)
        quint64 bytes = positions * sizeof(ObjectInfo);
        qDebug() << "Hash size is" << bytes << "holding" << m_size << "max nodes";
#endif
    sanityCheck();
}

template <class T>
inline void FixedSizeCache<T>::clear()
{
    int numberOfDeleted = 0;
    while (m_first) {
        ObjectInfo *delink = m_first;
        m_first = m_first->next;
        delete delink;
        ++numberOfDeleted;
    }

    while (m_unused) {
        ObjectInfo *delink = m_unused;
        m_unused = m_unused->next;
        delete delink;
        ++numberOfDeleted;
    }

    Q_ASSERT(numberOfDeleted == m_size);
    m_first = nullptr;
    m_last = nullptr;
    m_unused = nullptr;
    m_cache.clear();
    m_size = 0;
    m_used = 0;
}

template <class T>
inline void FixedSizeCache<T>::sanityCheck()
{
#if defined(DEBUG_SANITY)
    int numberOfUsed = 0;
    ObjectInfo *used = m_first;
    while (used) {
        used = used->next;
        ++numberOfUsed;
    }

    int numberOfUnused = 0;
    ObjectInfo *unused = m_unused;
    while (unused) {
        unused = unused->next;
        ++numberOfUnused;
    }

    Q_ASSERT(numberOfUsed == m_used);
    Q_ASSERT(numberOfUsed + numberOfUnused == m_size);
#endif
}

template <class T>
inline typename FixedSizeCache<T>::ObjectInfo* FixedSizeCache<T>::unlinkFromUsed()
{
    if (!m_last)
        return nullptr;

    Q_ASSERT(m_last);
    ObjectInfo *unpinned = m_last;

    while (unpinned && unpinned->pinned)
        unpinned = unpinned->previous;

    // If everything is pinned, then can only return nullptr
    if (!unpinned)
        return nullptr;

    ObjectInfo &info = *unpinned;

    // Remove from actual hash
    if (!info.object.deinitialize(true /*forcedFree*/))
        return nullptr;

    Q_ASSERT(m_cache.count(fixedHash(info.object)));
    m_cache.erase(fixedHash(info.object));

    // Update first and last
    if (m_first == &info) {
        // Make next the first
        m_first = info.next;
        if (m_first)
            m_first->previous = nullptr;
    } else if (m_last == &info) {
        // Make previous the last
        Q_ASSERT(info.previous);
        m_last = info.previous;
        m_last->next = nullptr;
    } else {
        // Link together info's previous and next
        if (info.previous)
            info.previous->next = info.next;
        if (info.next)
            info.next->previous = info.previous;
    }

    // Make naked
    info.previous = nullptr;
    info.next = nullptr;

    --m_used;
    return &info;
}

template <class T>
inline typename FixedSizeCache<T>::ObjectInfo* FixedSizeCache<T>::unlinkFromUnused()
{
    Q_ASSERT(m_unused);
    ObjectInfo &info = *m_unused;
    Q_ASSERT(!info.pinned);
    Q_ASSERT(!info.previous);

    // Update unused
    m_unused = info.next;
    if (m_unused)
        m_unused->previous = nullptr;

    // Make naked
    info.previous = nullptr;
    info.next = nullptr;

    return &info;
}

template <class T>
inline void FixedSizeCache<T>::linkToUsed(ObjectInfo &info)
{
    // Update first
    Q_ASSERT(!info.previous);
    info.next = m_first;
    if (m_first) {
        Q_ASSERT(!m_first->previous);
        m_first->previous = &info;
    }
    m_first = &info;

    // Update last
    if (!m_last)
        m_last = m_first;

    ++m_used;
    sanityCheck();
}

template <class T>
inline void FixedSizeCache<T>::relinkToUsed(ObjectInfo &info)
{
    // If info is already first, then nothing to be done
    if (m_first == &info)
        return;

    // Link together info's previous and next
    if (info.previous)
        info.previous->next = info.next;
    if (info.next)
        info.next->previous = info.previous;

    // Update last
    if (m_last == &info) {
        m_last = info.previous;
    }

    // Update first
    info.previous = nullptr;
    info.next = m_first;
    Q_ASSERT(m_first);
    m_first->previous = &info;
    m_first = &info;
    sanityCheck();
}

template <class T>
inline void FixedSizeCache<T>::relinkToUnused(ObjectInfo &info)
{
    // Remove from actual hash
    bool success = info.object.deinitialize(false /*forcedFree*/);
    Q_ASSERT(success);
    Q_ASSERT(m_cache.count(fixedHash(info.object)));
    m_cache.erase(fixedHash(info.object));

    // Possibly update first and last
    if (m_first == &info) {
        m_first = info.next;
    }

    if (m_last == &info) {
        m_last = info.previous;
    }

    // Link together info's previous and next
    if (info.previous)
        info.previous->next = info.next;
    if (info.next)
        info.next->previous = info.previous;

    // Update unused
    info.previous = nullptr;
    info.next = m_unused;
    if (m_unused) {
        Q_ASSERT(!m_unused->previous);
        m_unused->previous = &info;
    }
    m_unused = &info;
    --m_used;
    sanityCheck();
}

template <class T>
inline bool FixedSizeCache<T>::contains(quint64 hash) const
{
    Q_ASSERT(m_size);
    return m_cache.count(hash);
}

template <class T>
inline T *FixedSizeCache<T>::object(quint64 hash, bool update)
{
    Q_ASSERT(m_size);
    Q_ASSERT(m_cache.count(hash));

    ObjectInfo *info = m_cache.at(hash);
    if (!info)
        return nullptr;

    if (update)
        relinkToUsed(*info);
    return &(info->object);
}

template <class T>
inline T *FixedSizeCache<T>::newObject(quint64 hash)
{
    Q_ASSERT(m_size);
    Q_ASSERT(!m_cache.count(hash));

    ObjectInfo *info = nullptr;
    if (m_unused) {
        info = unlinkFromUnused();
    } else {
        info = unlinkFromUsed();
    }

    if (!info)
        return nullptr;

    m_cache.insert({hash, info});
    linkToUsed(*info);
    return &(info->object);
}

template <class T>
inline void FixedSizeCache<T>::unlink(quint64 hash)
{
    Q_ASSERT(m_size);
    Q_ASSERT(m_cache.count(hash));

    ObjectInfo *info = m_cache.at(hash);
    if (info->pinned)
        return;
    relinkToUnused(*info);
}

template <class T>
inline void FixedSizeCache<T>::pin(quint64 hash)
{
    Q_ASSERT(m_size);
    Q_ASSERT(m_cache.count(hash));

    ObjectInfo *info = m_cache.at(hash);
    info->pinned = true;
}

template <class T>
inline void FixedSizeCache<T>::unpin(quint64 hash)
{
    Q_ASSERT(m_size);
    Q_ASSERT(m_cache.count(hash));

    ObjectInfo *info = m_cache.at(hash);
    info->pinned = false;
}

template <class T>
inline float FixedSizeCache<T>::percentFull(int halfMoveNumber) const
{
    Q_ASSERT(m_size);
    Q_UNUSED(halfMoveNumber)
    return quint64(m_used) / float(m_size);
}

inline void Cache::reset()
{
    // Use a minimum of 100,000 positions
    int positions = qMax(Options::globalInstance()->option("Cache").value().toInt(), 100000);
    m_nodeCache.reset(positions);
    m_positionCache.reset(positions);
}

inline float Cache::percentFull(int halfMoveNumber) const
{
    return m_nodeCache.percentFull(halfMoveNumber);
}

inline int Cache::size() const
{
    Q_ASSERT(m_positionCache.size() == m_nodeCache.size());
    return m_nodeCache.size();
}

inline int Cache::used() const
{
    Q_ASSERT(m_positionCache.used() <= m_nodeCache.size());
    return m_nodeCache.used();
}

inline bool Cache::containsNode(quint64 hash) const
{
    return m_nodeCache.contains(hash);
}

inline Node *Cache::node(quint64 hash, bool relink)
{
    return m_nodeCache.object(hash, relink);
}

inline Node *Cache::newNode(quint64 hash)
{
    return m_nodeCache.newObject(hash);
}

inline void Cache::unlinkNode(quint64 hash)
{
    m_nodeCache.unlink(hash);
}

inline void Cache::pinNode(quint64 hash)
{
    m_nodeCache.pin(hash);
}

inline void Cache::unpinNode(quint64 hash)
{
    m_nodeCache.unpin(hash);
}

inline bool Cache::containsNodePosition(quint64 hash) const
{
    return m_positionCache.contains(hash);
}

inline Node::Position *Cache::nodePosition(quint64 hash, bool relink)
{
    return m_positionCache.object(hash, relink);
}

inline Node::Position *Cache::newNodePosition(quint64 hash)
{
    Node::Position *p = m_positionCache.newObject(hash);
    pinNodePosition(hash);
    return p;
}

inline void Cache::unlinkNodePosition(quint64 hash)
{
    m_positionCache.unlink(hash);
}

inline void Cache::pinNodePosition(quint64 hash)
{
    m_positionCache.pin(hash);
}

inline void Cache::unpinNodePosition(quint64 hash)
{
    m_positionCache.unpin(hash);
}

#endif // CACHE_H
