TODOs
=====

- mainline: account_luri_anonym
	b1d300653cf0075f6eeb656835a77f8f3fff3c0d
	https://commend.atlassian.net/browse/COR-1061
	- dropped already in rebase_v4.0.0

- audio: a config parameter for tx audio buffer size
```
commit 1e4548e3de13aac9da2f322e4c5166824dc270fc (origin/audio_buffer_tx, audio_buffer_tx)
Author: Wolfgang Netbal <w.netbal@commend.com>
Date:   Thu Oct 3 08:35:46 2019 +0200

    audio: a config parameter for tx audio buffer size

    - use tx audio_buffer_tx range setting instead of hard coded 30
    - the default max now can be reduced
```
	- PR was declined: https://github.com/baresip/baresip/pull/3460

```
commit fc401bdf9dc15bf766fb899f96f0d75c09474e15
Author: Wolfgang Netbal <w.netbal@commend.com>
Date:   Mon Jan 28 07:44:20 2019 +0100

    ua: set cuser depending on accounts cuser_ua (commend)

    Disable extension of UA pointer for cuser if account
    parameter cuser_ua is set to 0.
    Searching the correct UA for incoming calls has to be checked
    by host and port instead of cuser if cuser_ua is set to 0,
    because all UAs can have the same cuser and therefor the
    first one from the list matching will be used.

    Removing the issue on a Siemens HIpath.

    Only for Commend.
```
	- mainline PR was declined: https://github.com/baresip/baresip/pull/3462
	- replaced by: https://github.com/baresip/baresip/pull/3470

Command & Control
=================

- add prio to INVITE message
	- write RFC draft
	- implement in baresip

