# SOEM direct-mailbox port synchronization

Base: SOEM commit `d2b098f5a1806c69898473420cc52691b2ff017d`.

The application PDO thread and serialized SDO worker share a port. The Windows
and Linux patches make allocation, buffer-state publication, one packet send,
and one receive use the same existing `rx_mutex`. The unlocked receive
fast path is removed. Redundant sending uses `tx_mutex` then `rx_mutex`.
No lock surrounds an entire response wait, retry loop, or SDO transfer.

`SoemPortSynchronization.cmake` applies the patch to a fresh build-tree copy and
replaces only that source in the SOEM target. It does not modify the submodule;
configuration fails if an upstream change makes the patch inapplicable. Rebase
and review both platform patches when updating the dependency.

The new application runtime uses a single non-redundant NIC, bounds the number
of in-flight PDO frames plus mailbox traffic, and never enables cyclic mailbox
handling. Configuration, state access and error consumption are serialized.

Windows receive behavior was checked against local SDK documentation, rather
than inferred from the absence of `pcap_setnonblock`. The SOEM port passes a
read timeout of `-1` to `pcap_open`. Both `WpdPack_4_1_2.zip` at
`WpdPack/docs/html/group__wpcap__tut3.html` and `npcap-sdk-1.15.zip` at
`docs/npcap-tutorial.html` explicitly document that this makes adapter reads
return immediately. The archives used for this check were in the developer's
`Downloads/Compressed` directory. The existing timeout is retained.
`docs/wpcap/pcap_setnonblock.html` in the Npcap SDK also documents immediate
zero returns from `pcap_next_ex` when nonblocking mode has no packet available;
the bundled header and x64 import library expose that optional API, but an
additional call is not needed to establish the documented Windows behavior.

The Linux base port explicitly uses `MSG_DONTWAIT` for receive. Its socket setup
does not set `SO_SNDTIMEO` or `O_NONBLOCK`, and its original two `send` calls use
zero flags. The Linux patch also changes both sends to `MSG_DONTWAIT`, so buffer
pressure returns an error through the existing failure/retry path instead of
waiting with the shared port lock held. No socket-wide timeout or blocking-mode
change is introduced.
