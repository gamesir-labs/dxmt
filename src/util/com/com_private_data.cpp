/* 
 * This file is part of DXMT, Copyright (c) 2023 Feifan He
 * 
 * Derived from a part of DXVK (originally under zlib License), 
 * Copyright (c) 2017 Philip Rebohle
 * Copyright (c) 2019 Joshua Ashton
 *
 * See <https://github.com/doitsujin/dxvk/blob/master/LICENSE>
 */

#include <cstring>
#include <d3dcommon.h>

#include "com_private_data.hpp"

namespace dxmt {

namespace {

static const GUID kDebugObjectNameWGuid = {
    0x4cca5fd8, 0x921f, 0x42c8, {0x85, 0x66, 0x70, 0xca, 0xf2, 0xa9, 0xb7, 0x41}};

}

ComPrivateDataEntry::ComPrivateDataEntry() = default;

ComPrivateDataEntry::ComPrivateDataEntry(REFGUID guid, UINT size,
                                         const void *data)
    : m_guid(guid),
      m_value(Data(static_cast<const uint8_t *>(data),
                   static_cast<const uint8_t *>(data) + size)) {}

ComPrivateDataEntry::ComPrivateDataEntry(REFGUID guid, const IUnknown *iface)
    : m_guid(guid), m_value(Com<IUnknown>(const_cast<IUnknown *>(iface))) {}

HRESULT ComPrivateDataEntry::get(UINT &size, void *data) const {
  const auto *bytes = std::get_if<Data>(&m_value);
  const auto *iface = std::get_if<Com<IUnknown>>(&m_value);
  const UINT minSize = bytes ? static_cast<UINT>(bytes->size())
                             : iface ? sizeof(IUnknown *) : 0;

  if (!data) {
    size = minSize;
    return S_OK;
  }

  HRESULT result = size < minSize ? DXGI_ERROR_MORE_DATA : S_OK;

  if (size >= minSize) {
    if (iface) {
      IUnknown *value = iface->ref();
      std::memcpy(data, &value, minSize);
    } else if (bytes && minSize) {
      std::memcpy(data, bytes->data(), minSize);
    }
  }

  size = minSize;
  return result;
}

HRESULT ComPrivateData::setData(REFGUID guid, UINT size, const void *data) {
  std::lock_guard lock(m_mutex);
  if (!data) {
    for (auto it = m_entries.begin(); it != m_entries.end(); ++it) {
      if (it->hasGuid(guid)) {
        m_entries.erase(it);
        return S_OK;
      }
    }
    return S_FALSE;
  }
  this->insertEntry(ComPrivateDataEntry(guid, size, data));
  return S_OK;
}

HRESULT ComPrivateData::setInterface(REFGUID guid, const IUnknown *iface) {
  if (!iface)
    return setData(guid, 0, nullptr);

  std::lock_guard lock(m_mutex);
  this->insertEntry(ComPrivateDataEntry(guid, iface));
  return S_OK;
}

HRESULT ComPrivateData::setName(const WCHAR *name) {
  if (!name)
    return setData(kDebugObjectNameWGuid, 0, nullptr);

  UINT length = 0;
  while (name[length])
    length++;

  return setData(kDebugObjectNameWGuid, (length + 1) * sizeof(WCHAR), name);
}

HRESULT ComPrivateData::getData(REFGUID guid, UINT *size, void *data) {
  if (!size)
    return E_INVALIDARG;

  std::lock_guard lock(m_mutex);
  auto entry = this->findEntry(guid);

  if (!entry) {
    *size = 0;
    return DXGI_ERROR_NOT_FOUND;
  }

  return entry->get(*size, data);
}

ComPrivateDataEntry *ComPrivateData::findEntry(REFGUID guid) {
  for (ComPrivateDataEntry &e : m_entries) {
    if (e.hasGuid(guid))
      return &e;
  }

  return nullptr;
}

void ComPrivateData::insertEntry(ComPrivateDataEntry &&entry) {
  ComPrivateDataEntry *dstEntry = this->findEntry(entry.guid());

  if (dstEntry)
    *dstEntry = std::move(entry);
  else
    m_entries.push_back(std::move(entry));
}

} // namespace dxmt
