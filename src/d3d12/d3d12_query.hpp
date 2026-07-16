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

inline bool IsQueryTypeCompatible(D3D12_QUERY_HEAP_TYPE heap_type,
                                  D3D12_QUERY_TYPE query_type) {
  switch (heap_type) {
  case D3D12_QUERY_HEAP_TYPE_OCCLUSION:
    return query_type == D3D12_QUERY_TYPE_OCCLUSION ||
           query_type == D3D12_QUERY_TYPE_BINARY_OCCLUSION;
  case D3D12_QUERY_HEAP_TYPE_TIMESTAMP:
    return query_type == D3D12_QUERY_TYPE_TIMESTAMP;
  case D3D12_QUERY_HEAP_TYPE_PIPELINE_STATISTICS:
    return query_type == D3D12_QUERY_TYPE_PIPELINE_STATISTICS;
  case D3D12_QUERY_HEAP_TYPE_SO_STATISTICS:
    return query_type == D3D12_QUERY_TYPE_SO_STATISTICS_STREAM0 ||
           query_type == D3D12_QUERY_TYPE_SO_STATISTICS_STREAM1 ||
           query_type == D3D12_QUERY_TYPE_SO_STATISTICS_STREAM2 ||
           query_type == D3D12_QUERY_TYPE_SO_STATISTICS_STREAM3;
  default:
    return false;
  }
}

bool ResolveQuerySnapshot(const QueryResolveSnapshot &snapshot,
                          std::vector<uint8_t> &data);

class QueryHeap {
public:
  virtual ~QueryHeap() = default;

  virtual const D3D12_QUERY_HEAP_DESC &GetDesc() const = 0;
  virtual IMTLD3D12Device *GetParentDevice() const = 0;
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
