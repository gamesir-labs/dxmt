/*
 * This file is part of DXMT, Copyright (c) 2023 Feifan He
 *
 * Derived from a part of DXVK (originally under zlib License),
 * Copyright (c) 2017 Philip Rebohle
 * Copyright (c) 2019 Joshua Ashton
 *
 * See <https://github.com/doitsujin/dxvk/blob/master/LICENSE>
 */
#pragma once

#include "com_pointer.hpp"

#include <cstdint>
#include <mutex>
#include <variant>
#include <vector>
#include <unknwn.h>

namespace dxmt {

/**
 * \brief Data entry for private storage
 * Stores a single private storage item.
 */
class ComPrivateDataEntry {

public:
  ComPrivateDataEntry();
  ComPrivateDataEntry(REFGUID guid, UINT size, const void *data);
  ComPrivateDataEntry(REFGUID guid, const IUnknown *iface);

  ComPrivateDataEntry(ComPrivateDataEntry &&other) noexcept = default;
  ComPrivateDataEntry &operator=(ComPrivateDataEntry &&other) noexcept = default;

  /**
   * \brief The entry's GUID
   * \returns The GUID
   */
  REFGUID guid() const { return m_guid; }

  /**
   * \brief Checks whether the GUID matches another one
   *
   * GUIDs are used to identify private data entries.
   * \param [in] guid The GUID to compare to
   * \returns \c true if this entry holds the same GUID
   */
  bool hasGuid(REFGUID guid) const { return m_guid == guid; }

  /**
   * \brief Retrieves stored data
   *
   * \param [in,out] size Destination buffer size
   * \param [in] data Appliaction-provided buffer
   * \returns \c S_OK on success, or \c DXGI_ERROR_MORE_DATA
   *          if the destination buffer is too small
   */
  HRESULT get(UINT &size, void *data) const;

private:
  using Data = std::vector<uint8_t>;

  GUID m_guid = __uuidof(IUnknown);
  std::variant<std::monostate, Data, Com<IUnknown>> m_value;
};

/**
 * \brief Private storage for DXGI objects
 *
 * Provides storage for application-defined
 * byte arrays or COM interfaces that can be
 * retrieved using GUIDs.
 */
class ComPrivateData {

public:
  HRESULT setData(REFGUID guid, UINT size, const void *data);

  HRESULT setInterface(REFGUID guid, const IUnknown *iface);

  HRESULT setName(const WCHAR *name);

  HRESULT getData(REFGUID guid, UINT *size, void *data);

private:
  std::mutex m_mutex;
  std::vector<ComPrivateDataEntry> m_entries;

  ComPrivateDataEntry *findEntry(REFGUID guid);
  void insertEntry(ComPrivateDataEntry &&entry);
};

} // namespace dxmt
