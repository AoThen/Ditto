#pragma once
#include <list>
#include <unordered_map>
#include <string>

class CPinyinConvertCache
{
    static constexpr size_t MAX_ENTRIES = 10000;

    using Entry = std::pair<long, std::pair<std::string, std::string>>;
    std::list<Entry> m_items;
    std::unordered_map<long, std::list<Entry>::iterator> m_map;

public:
    const std::pair<std::string, std::string>* Get(long id)
    {
        auto it = m_map.find(id);
        if (it == m_map.end())
            return nullptr;

        m_items.splice(m_items.begin(), m_items, it->second);
        return &it->second->second;
    }

    void Put(long id, std::pair<std::string, std::string> val)
    {
        if (m_map.find(id) != m_map.end())
            return;

        if (m_items.size() >= MAX_ENTRIES)
        {
            m_map.erase(m_items.back().first);
            m_items.pop_back();
        }
        m_items.emplace_front(id, std::move(val));
        m_map[id] = m_items.begin();
    }
};