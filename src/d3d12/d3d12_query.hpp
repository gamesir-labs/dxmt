#pragma once

#include "d3d12_device.hpp"
#include "com/com_pointer.hpp"
#include "dxmt_occlusion_query.hpp"
#include <d3d12.h>
#include <vector>

namespace dxmt::d3d12 {

class QueryHeap;

struct QueryResolveSnapshotEntry {
  bool ended = false;
  Rc<VisibilityResultQuery> visibility;
  Rc<TimestampQuery> timestamp;
};

struct QueryResolveSnapshot {
  D3D12_QUERY_TYPE type = D3D12_QUERY_TYPE_OCCLUSION;
  std::vector<QueryResolveSnapshotEntry> entries;
};

bool ResolveQuerySnapshot(const QueryResolveSnapshot &snapshot,
                          std::vector<uint8_t> &data);

class QueryHeap {
public:
  virtual ~QueryHeap() = default;

  virtual const D3D12_QUERY_HEAP_DESC &GetDesc() const = 0;
  virtual Rc<VisibilityResultQuery> BeginVisibility(D3D12_QUERY_TYPE type,
                                                    UINT index) = 0;
  virtual Rc<VisibilityResultQuery> EndVisibility(D3D12_QUERY_TYPE type,
                                                  UINT index) = 0;
  virtual Rc<TimestampQuery> EndTimestamp(D3D12_QUERY_TYPE type,
                                          UINT index) = 0;
  virtual bool BeginStatistics(D3D12_QUERY_TYPE type, UINT index) = 0;
  virtual bool EndStatistics(D3D12_QUERY_TYPE type, UINT index) = 0;
  virtual bool CaptureResolve(D3D12_QUERY_TYPE type, UINT start_index,
                              UINT query_count,
                              QueryResolveSnapshot &snapshot) const = 0;
};

Com<ID3D12QueryHeap>
CreateQueryHeap(IMTLD3D12Device *device, const D3D12_QUERY_HEAP_DESC *desc);

} // namespace dxmt::d3d12
