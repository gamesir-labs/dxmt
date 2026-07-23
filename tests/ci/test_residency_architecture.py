#!/usr/bin/env python3

import pathlib
import re
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[2]
DXMT_QUEUE = (ROOT / "src/dxmt/dxmt_command_queue.cpp").read_text()
WINEMETAL = (ROOT / "src/winemetal4/unix/winemetal_unix.c").read_text()


def braced_body(source: str, marker: str) -> str:
    start = source.find(marker)
    if start < 0:
        raise AssertionError(f"missing source marker: {marker}")
    brace = source.find("{", start)
    if brace < 0:
        raise AssertionError(f"missing body after source marker: {marker}")
    depth = 0
    for index in range(brace, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[brace + 1:index]
    raise AssertionError(f"unterminated body after source marker: {marker}")


class ResidencyArchitecturePolicyTest(unittest.TestCase):
    def test_queue_owns_and_attaches_exactly_one_persistent_set(self):
        self.assertEqual(DXMT_QUEUE.count("newResidencySet("), 1)
        self.assertEqual(DXMT_QUEUE.count(".requestResidency()"), 1)
        self.assertEqual(DXMT_QUEUE.count(".addResidencySet("), 1)
        self.assertIn("persistent_residency_set_(device.newResidencySet(", DXMT_QUEUE)
        self.assertIn(
            "persistent_residency_set_.requestResidency()", DXMT_QUEUE
        )
        self.assertIn(
            "commandQueue.addResidencySet(persistent_residency_set_)",
            DXMT_QUEUE,
        )

    def test_command_buffer_encode_does_not_rebuild_residency_sets(self):
        encode = braced_body(DXMT_QUEUE, "CommandQueue::CommitChunkInternal(")
        for forbidden in (
            "newResidencySet(",
            "requestResidency(",
            "addResidencySet(",
            "useResidencySet(",
        ):
            self.assertNotIn(forbidden, encode)

    def test_completion_retires_resources_without_committing_the_set(self):
        completion = braced_body(DXMT_QUEUE, "CommandQueue::WaitForFinishThread(")
        retire = braced_body(
            DXMT_QUEUE, "CommandQueue::RetirePersistentResidencyRemovals("
        )
        self.assertNotIn("persistent_residency_set_.commit()", completion)
        self.assertNotIn("FlushPersistentResidency(", completion)
        self.assertNotIn("persistent_residency_set_.commit()", retire)
        signal = completion.index("cpu_coherent.signal(")
        retire_call = completion.index("RetirePersistentResidencyRemovals(")
        deferred = completion.index("CompleteDeferredReleases(")
        self.assertLess(signal, retire_call)
        self.assertLess(retire_call, deferred)

    def test_temporary_releases_are_keyed_to_completed_sequences(self):
        retain = braced_body(
            DXMT_QUEUE,
            "CommandQueue::RetainUntilGpuComplete(uint64_t sequence",
        )
        complete = braced_body(
            DXMT_QUEUE, "CommandQueue::CompleteDeferredReleases("
        )
        self.assertIn("deferred_releases_[sequence]", retain)
        self.assertIn("sequence <= deferred_release_completed_seq_", retain)
        self.assertIn("it->first <= deferred_release_completed_seq_", complete)
        self.assertIn("releases.push_back(std::move(release))", complete)

    def test_persistent_membership_is_reference_counted(self):
        add = braced_body(DXMT_QUEUE, "CommandQueue::AddPersistentResidency(")
        remove = braced_body(
            DXMT_QUEUE,
            "CommandQueue::RemovePersistentResidencyAfterCompletion(\n"
            "    WMT::Object allocation, uint64_t sequence)",
        )
        self.assertIn("entry.ref_count++", add)
        self.assertIn("entry->second.ref_count--", remove)
        self.assertRegex(
            remove,
            re.compile(
                r"if\s*\(entry->second\.ref_count\)\s*return\s*;",
                re.MULTILINE,
            ),
        )

    def test_winemetal_has_no_command_buffer_residency_path(self):
        for forbidden in (
            "_residencySet",
            "prepareResidencyForCommit",
            "releaseSparseResidencyAllocations",
        ):
            self.assertNotIn(forbidden, WINEMETAL)
        self.assertNotRegex(
            WINEMETAL,
            re.compile(r"@synchronized\s*\(\s*(?:_owner|queue)\s*\)"),
        )

    def test_sparse_mapping_is_ordered_by_queue_events(self):
        mapping = braced_body(
            WINEMETAL, "_MTLCommandQueue_updateSparseTextureMappings("
        )
        wait = mapping.index("waitForEvent:")
        update = mapping.index("updateTextureMappings:")
        signal = mapping.index("signalEvent:")
        self.assertLess(wait, update)
        self.assertLess(update, signal)
        self.assertIn(
            "[queue.sparseTrackedTextures removeObject:texture]", mapping
        )
        logical_remove = mapping.index(
            "[queue.sparseTrackedTextures removeObject:texture]"
        )
        retire_texture = mapping.index(
            "[retiredAllocations addObject:(id<MTLAllocation>)texture]"
        )
        self.assertLess(signal, logical_remove)
        self.assertLess(logical_remove, retire_texture)

    def test_completion_only_releases_retained_allocations(self):
        implementation = WINEMETAL[
            WINEMETAL.index("@implementation DXMTMetal4CommandBuffer") :
        ]
        wait = braced_body(implementation, "- (void)waitUntilCompleted")
        self.assertIn("[self releaseRetainedAllocations]", wait)
        self.assertNotIn("releaseSparseResidencyAllocations", wait)
        self.assertNotIn("ResidencySet", wait)


if __name__ == "__main__":
    unittest.main()
