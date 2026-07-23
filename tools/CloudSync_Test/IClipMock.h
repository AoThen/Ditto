#pragma once
#include "../../src/shared/IClip.h"
#include <vector>
#include <memory>
#include <cstring>

class CMockClipFormat : public IClipFormat
{
public:
    CMockClipFormat(CLIPFORMAT type, const char* text)
        : m_type(type), m_hData(nullptr)
    {
        if (text)
        {
            SIZE_T size = strlen(text) + 1;
            m_hData = GlobalAlloc(GMEM_MOVEABLE, size);
            if (m_hData)
            {
                void* p = GlobalLock(m_hData);
                if (p)
                {
                    memcpy(p, text, size);
                    GlobalUnlock(m_hData);
                }
            }
        }
    }

    CMockClipFormat(CLIPFORMAT type, HGLOBAL hData)
        : m_type(type), m_hData(hData)
    {
    }

    ~CMockClipFormat() override
    {
        Free();
    }

    CLIPFORMAT Type() override { return m_type; }
    void Type(CLIPFORMAT type) override { m_type = type; }

    HGLOBAL Data() override { return m_hData; }
    void Data(HGLOBAL data) override
    {
        if (m_hData) GlobalFree(m_hData);
        m_hData = data;
    }

    bool AutoDeleteData() override { return false; }
    void AutoDeleteData(bool /*autoDelete*/) override {}

    void Free() override
    {
        if (m_hData)
        {
            GlobalFree(m_hData);
            m_hData = nullptr;
        }
    }

    CStringA GetAsCStringA() override
    {
        if (!m_hData) return CStringA();
        char* p = (char*)GlobalLock(m_hData);
        if (!p) return CStringA();
        CStringA result(p);
        GlobalUnlock(m_hData);
        return result;
    }

    CString GetAsCString() override
    {
        return CString(GetAsCStringA());
    }

    Gdiplus::Bitmap* CreateGdiplusBitmap() override
    {
        return nullptr;
    }

private:
    CLIPFORMAT m_type;
    HGLOBAL m_hData;
};

class CMockClipFormats : public IClipFormats
{
public:
    int Size() override { return (int)m_formats.size(); }

    IClipFormat* GetAt(int nPos) override
    {
        if (nPos >= 0 && nPos < (int)m_formats.size())
            return m_formats[nPos].get();
        return nullptr;
    }

    void DeleteAt(int nPos) override
    {
        if (nPos >= 0 && nPos < (int)m_formats.size())
            m_formats.erase(m_formats.begin() + nPos);
    }

    void DeleteAll() override
    {
        m_formats.clear();
    }

    INT_PTR AddNew(CLIPFORMAT type, HGLOBAL data) override
    {
        auto fmt = std::make_unique<CMockClipFormat>(type, data);
        m_formats.push_back(std::move(fmt));
        return m_formats.size() - 1;
    }

    IClipFormat* FindFormatEx(CLIPFORMAT type) override
    {
        for (auto& f : m_formats)
            if (f->Type() == type)
                return f.get();
        return nullptr;
    }

    bool RemoveFormat(CLIPFORMAT type) override
    {
        for (auto it = m_formats.begin(); it != m_formats.end(); ++it)
        {
            if ((*it)->Type() == type)
            {
                m_formats.erase(it);
                return true;
            }
        }
        return false;
    }

    void AddTextFormat(CLIPFORMAT type, const char* text)
    {
        m_formats.push_back(std::make_unique<CMockClipFormat>(type, text));
    }

private:
    std::vector<std::unique_ptr<CMockClipFormat>> m_formats;
};

class CMockClip : public IClip
{
public:
    CString Description() override { return m_description; }
    void Description(CString csValue) override { m_description = csValue; }

    CTime PasteTime() override { return CTime::GetCurrentTime(); }

    int ID() override { return m_id; }
    void Parent(int nParent) override { m_parent = nParent; }
    int Parent() override { return m_parent; }

    int DontAutoDelete() override { return m_dontAutoDelete; }
    void DontAutoDelete(int Dont) override { m_dontAutoDelete = Dont; }

    CString QuickPaste() override { return m_quickPaste; }
    void QuickPaste(CString csValue) override { m_quickPaste = csValue; }

    void SetSaveToDbSticky(AddToDbStickyEnum::AddToDbSticky option) override
    {
        m_stickyOption = option;
    }

    IClipFormats* Clips() override
    {
        return &m_formats;
    }

    CMockClipFormats& Formats() { return m_formats; }

private:
    CString m_description;
    int m_id = 0;
    int m_parent = 0;
    int m_dontAutoDelete = 0;
    CString m_quickPaste;
    AddToDbStickyEnum::AddToDbSticky m_stickyOption = AddToDbStickyEnum::INVALID;
    CMockClipFormats m_formats;
};
