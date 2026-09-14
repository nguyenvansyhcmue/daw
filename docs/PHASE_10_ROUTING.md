# Phase 10: Routing Foundation

Tracks can now route by stable `BusId`: directly to master, to a bus, or directly to master with a post-fader send to a bus. Buses are represented in the immutable render structure; the callback uses preallocated bus buffers, then sums bus outputs to the existing master graph.

Routes are identity-based and remain independent of track reorder. Bus removal clears dependent routes. The targeted test verifies direct, send and bus-output gain accumulation exactly once.

Current limitations: no bus FX, bus solo controls, pre-fader sends, persistence or routing UI. The fixed current engine capacity remains eight tracks and eight buses.
