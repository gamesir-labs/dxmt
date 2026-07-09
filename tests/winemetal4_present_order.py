#!/usr/bin/env python3

from pathlib import Path
import sys


def require(condition, message):
    if not condition:
        print(f"not ok - {message}")
        return False
    print(f"ok - {message}")
    return True


def main():
    repo_root = Path(__file__).resolve().parents[1]
    source = repo_root / "src" / "winemetal4" / "unix" / "winemetal_unix.c"
    text = source.read_text(encoding="utf-8")

    end_command_buffer = text.find("[_metal4Buffer endCommandBuffer]")
    wait_drawable = text.find("[_owner.metal4Queue waitForDrawable:_pendingDrawable]")
    commit = text.find("[_owner.metal4Queue commit:commandBuffers count:1")
    owner_signal = text.find("[_owner.metal4Queue signalEvent:_owner.event value:_completionValue]")
    signal_drawable = text.find("[_owner.metal4Queue signalDrawable:_pendingDrawable]")
    present_after_duration = text.find("[_pendingDrawable presentAfterMinimumDuration:_presentDuration]")
    present = text.find("[_pendingDrawable present]")

    ok = True
    ok &= require(end_command_buffer >= 0, "command-buffer finalization remains represented")
    ok &= require(wait_drawable >= 0, "drawable wait remains represented")
    ok &= require(commit >= 0, "Metal4 commit remains represented")
    ok &= require(owner_signal >= 0, "owner completion signal remains represented")
    ok &= require(signal_drawable >= 0, "drawable signal remains represented")
    ok &= require(present_after_duration >= 0, "duration-based present remains represented")
    ok &= require(present >= 0, "ordinary present remains represented")

    ok &= require("addPresentedHandler" not in text,
                  "present completion does not depend on drawable addPresentedHandler")
    ok &= require("presentEvent" not in text,
                  "Metal4 present path does not create a drawable-presented event")
    ok &= require("DXMT_METAL4_PRESENT_ORDERING" not in text,
                  "Metal4 present path does not keep a private present-order knob")
    ok &= require("dxmt_metal4_present_ordering_enabled" not in text,
                  "Metal4 present path does not keep a private present-order wait/signal path")

    if ok:
        ok &= require(
            end_command_buffer < wait_drawable < commit,
            "drawable wait is enqueued after command-buffer finalization and before commit",
        )
        ok &= require(
            commit < owner_signal < signal_drawable,
            "queue completion is signaled before signalDrawable",
        )
        ok &= require(
            signal_drawable < present_after_duration,
            "duration-based present is enqueued after signalDrawable",
        )
        ok &= require(
            signal_drawable < present,
            "ordinary present is enqueued after signalDrawable",
        )

    feedback_start = text.find("addFeedbackHandler")
    feedback_end = text.find("}];", feedback_start)
    ok &= require(feedback_start >= 0 and feedback_end > feedback_start,
                  "Metal4 feedback handler is present for apitrace recording")
    if feedback_start >= 0 and feedback_end > feedback_start:
        feedback_block = text[feedback_start:feedback_end]
        ok &= require("_owner.event" not in feedback_block and "signalEvent" not in feedback_block,
                      "apitrace feedback handler does not drive queue completion")

    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
