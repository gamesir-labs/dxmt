// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
#include "DXBCUtils.h"
#include "BlobContainer.h"

#include <cstdint>
#include <cstring>

namespace microsoft {

namespace {

UINT32 ReadUInt32(const void *address) {
    UINT32 value;
    std::memcpy(&value, address, sizeof(value));
    return value;
}

}

//=================================================================================================================================
// CDXBCParser

//---------------------------------------------------------------------------------------------------------------------------------
// CDXBCParser::CDXBCParser
CDXBCParser::CDXBCParser()
{
    m_pHeader = NULL;
    m_pIndex = NULL;
}

//---------------------------------------------------------------------------------------------------------------------------------
// CDXBCParser::ReadDXBC()
HRESULT CDXBCParser::ReadDXBC( const void* pContainer, UINT ContainerSizeInBytes )
{
    m_pHeader = NULL;
    m_pIndex = NULL;

    if (!pContainer)
    {
        return E_FAIL;
    }
    if (ContainerSizeInBytes < sizeof( DXBCHeader ))
    {
        return E_FAIL;
    }
    const DXBCHeader* pHeader = static_cast<const DXBCHeader*>(pContainer);
    if (pHeader->ContainerSizeInBytes != ContainerSizeInBytes)
    {
        return E_FAIL;
    }
    if ((pHeader->DXBCHeaderFourCC != DXBC_FOURCC_NAME) ||
         (pHeader->Version.Major != DXBC_MAJOR_VERSION) ||
         (pHeader->Version.Minor != DXBC_MINOR_VERSION))
    {
        return E_FAIL;
    }
    const uint64_t indexEnd = uint64_t(sizeof(DXBCHeader)) +
                              uint64_t(pHeader->BlobCount) * sizeof(UINT32);
    if (indexEnd > ContainerSizeInBytes)
    {
        return E_FAIL;
    }
    const BYTE* bytes = static_cast<const BYTE*>(pContainer);
    const BYTE* pIndex = bytes + sizeof(DXBCHeader);
    uint64_t expectedBlobOffset = indexEnd;

    for (UINT b = 0; b < pHeader->BlobCount; b++)
    {
        const uint64_t blobOffset = ReadUInt32(pIndex + b * sizeof(UINT32));
        if (blobOffset != expectedBlobOffset ||
            blobOffset + sizeof(DXBCBlobHeader) > ContainerSizeInBytes)
        {
            return E_FAIL;
        }

        const UINT32 blobSize = ReadUInt32(
            bytes + blobOffset + offsetof(DXBCBlobHeader, BlobSize));
        expectedBlobOffset = blobOffset + sizeof(DXBCBlobHeader) +
                             uint64_t(blobSize);
        if (expectedBlobOffset > ContainerSizeInBytes)
            return E_FAIL;
    }

    // Ok, satisfied with integrity of container, store info.
    m_pHeader = pHeader;
    m_pIndex = reinterpret_cast<const UINT32*>(pIndex);
    return S_OK;
}

//---------------------------------------------------------------------------------------------------------------------------------
// CDXBCParser::ReadDXBCAssumingValidSize()
HRESULT CDXBCParser::ReadDXBCAssumingValidSize( const void* pContainer )
{
    if (!pContainer)
    {
        return ReadDXBC(NULL, 0);
    }
    return ReadDXBC( (const BYTE*)pContainer, DXBCGetSizeAssumingValidPointer( (const BYTE*)pContainer ) );
}

//---------------------------------------------------------------------------------------------------------------------------------
// CDXBCParser::FindNextMatchingBlob
UINT CDXBCParser::FindNextMatchingBlob( DXBCFourCC SearchFourCC, UINT SearchStartBlobIndex )
{
    if (!m_pHeader || !m_pIndex)
    {
        return (UINT)DXBC_BLOB_NOT_FOUND;
    }
    for (UINT b = SearchStartBlobIndex; b < m_pHeader->BlobCount; b++)
    {
        const BYTE* index = reinterpret_cast<const BYTE*>(m_pIndex) +
                            b * sizeof(UINT32);
        const BYTE* blob = reinterpret_cast<const BYTE*>(m_pHeader) +
                           ReadUInt32(index);
        if (ReadUInt32(blob + offsetof(DXBCBlobHeader, BlobFourCC)) ==
            static_cast<UINT32>(SearchFourCC))
        {
            return b;
        }
    }
    return (UINT)DXBC_BLOB_NOT_FOUND;
}

//---------------------------------------------------------------------------------------------------------------------------------
// CDXBCParser::GetVersion()
const DXBCVersion* CDXBCParser::GetVersion()
{
    return m_pHeader ? &m_pHeader->Version : NULL;
}

//---------------------------------------------------------------------------------------------------------------------------------
// CDXBCParser::GetHash()
const DXBCHash* CDXBCParser::GetHash()
{
    return m_pHeader ? &m_pHeader->Hash : NULL;
}

//---------------------------------------------------------------------------------------------------------------------------------
// CDXBCParser::GetBlobCount()
UINT CDXBCParser::GetBlobCount()
{
    return m_pHeader ? m_pHeader->BlobCount : 0;
}

//---------------------------------------------------------------------------------------------------------------------------------
// CDXBCParser::GetBlob()
const void* CDXBCParser::GetBlob( UINT BlobIndex )
{
    if (!m_pHeader || !m_pIndex || m_pHeader->BlobCount <= BlobIndex)
    {
        return NULL;
    }
    const BYTE* index = reinterpret_cast<const BYTE*>(m_pIndex) +
                        BlobIndex * sizeof(UINT32);
    return reinterpret_cast<const BYTE*>(m_pHeader) + ReadUInt32(index) +
           sizeof(DXBCBlobHeader);
}

//---------------------------------------------------------------------------------------------------------------------------------
// CDXBCParser::GetBlobSize()
UINT CDXBCParser::GetBlobSize( UINT BlobIndex )
{
    if (!m_pHeader || !m_pIndex || m_pHeader->BlobCount <= BlobIndex)
    {
        return 0;
    }
    const BYTE* index = reinterpret_cast<const BYTE*>(m_pIndex) +
                        BlobIndex * sizeof(UINT32);
    const BYTE* blob = reinterpret_cast<const BYTE*>(m_pHeader) +
                       ReadUInt32(index);
    return ReadUInt32(blob + offsetof(DXBCBlobHeader, BlobSize));
}

//---------------------------------------------------------------------------------------------------------------------------------
// CDXBCParser::GetBlobFourCC()
UINT CDXBCParser::GetBlobFourCC( UINT BlobIndex )
{
    if (!m_pHeader || !m_pIndex || m_pHeader->BlobCount <= BlobIndex)
    {
        return 0;
    }
    const BYTE* index = reinterpret_cast<const BYTE*>(m_pIndex) +
                        BlobIndex * sizeof(UINT32);
    const BYTE* blob = reinterpret_cast<const BYTE*>(m_pHeader) +
                       ReadUInt32(index);
    return ReadUInt32(blob + offsetof(DXBCBlobHeader, BlobFourCC));
}

//---------------------------------------------------------------------------------------------------------------------------------
// CDXBCParser::RelocateBytecode()
HRESULT CDXBCParser::RelocateBytecode( UINT ByteOffset )
{
    if (!m_pHeader || !m_pIndex)
    {
        // bad -- has not been initialized yet
        return E_FAIL;
    }
    m_pHeader = (const DXBCHeader*)((const BYTE*)m_pHeader + ByteOffset);
    m_pIndex = (const UINT32*)((const BYTE*)m_pIndex + ByteOffset);
    return S_OK;
}

}
